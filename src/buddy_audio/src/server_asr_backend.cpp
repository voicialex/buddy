#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>

#include "buddy_audio/asr_backend.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class ServerAsrBackend : public AsrBackend {
public:
    ~ServerAsrBackend() override { shutdown(); }

    bool initialize(const AsrBackendConfig& config, rclcpp::Logger logger) override {
        logger_ = logger;
        url_ = config.server_url;
        mode_ = config.server_mode;
        sample_rate_ = config.sample_rate;

        if (url_.empty()) {
            RCLCPP_ERROR(logger_, "ServerAsrBackend: server_url is empty");
            return false;
        }

        if (!connect()) {
            RCLCPP_WARN(logger_, "ServerAsrBackend: initial connect failed, will retry on feed");
        }

        RCLCPP_INFO(logger_, "ServerAsrBackend ready (url=%s, mode=%s)", url_.c_str(), mode_.c_str());
        return true;
    }

    AsrResult feed(const float* samples, int n) override {
        AsrResult result;
        if (paused_ || n <= 0) return result;

        if (!connected_ && !connect()) return result;

        try {
            // Send audio as PCM16 binary
            std::vector<int16_t> pcm16(n);
            for (int i = 0; i < n; ++i) {
                float s = samples[i] * 32768.0f;
                if (s > 32767.0f) s = 32767.0f;
                if (s < -32768.0f) s = -32768.0f;
                pcm16[i] = static_cast<int16_t>(s);
            }

            {
                std::lock_guard<std::mutex> lock(ws_mutex_);
                if (ws_ && connected_) {
                    ws_->binary(true);
                    ws_->write(net::buffer(pcm16.data(), pcm16.size() * sizeof(int16_t)));
                }
            }

            // Drain result queue
            std::lock_guard<std::mutex> lock(result_mutex_);
            if (!result_queue_.empty()) {
                result = result_queue_.front();
                result_queue_.pop();
            }
        } catch (const std::exception& e) {
            RCLCPP_WARN(logger_, "ServerAsrBackend: WebSocket write error: %s", e.what());
            connected_ = false;
        }
        return result;
    }

    void reset() override {
        shutdown();
        connect();
    }

    void pause(bool paused) override { paused_ = paused; }

private:
    bool connect() {
        // Clean up previous connection
        stop_reader();

        try {
            std::string host, port;
            parse_url(url_, host, port);

            ioc_ = std::make_unique<net::io_context>();
            ws_ = std::make_unique<websocket::stream<beast::tcp_stream>>(*ioc_);

            tcp::resolver resolver(*ioc_);
            auto results = resolver.resolve(host, port);
            beast::get_lowest_layer(*ws_).connect(results);
            ws_->handshake(host, "/");

            // Send initial config frame
            std::string config_json = "{\"mode\":\"" + mode_ +
                                      "\",\"chunk_size\":[5,10,5]"
                                      ",\"wav_name\":\"buddy\""
                                      ",\"is_speaking\":true"
                                      ",\"wav_format\":\"pcm\""
                                      ",\"audio_fs\":" +
                                      std::to_string(sample_rate_) + "}";
            ws_->text(true);
            ws_->write(net::buffer(config_json));

            connected_ = true;
            start_reader();
            return true;
        } catch (const std::exception& e) {
            RCLCPP_WARN(logger_, "ServerAsrBackend: connect failed: %s", e.what());
            connected_ = false;
            return false;
        }
    }

    void start_reader() {
        reader_running_ = true;
        reader_thread_ = std::thread([this]() {
            while (reader_running_ && connected_) {
                try {
                    {
                        std::lock_guard<std::mutex> lock(ws_mutex_);
                        if (!ws_ || !connected_) break;
                        beast::get_lowest_layer(*ws_).expires_after(std::chrono::milliseconds(200));
                    }

                    // Read WITHOUT the lock — only one reader thread exists,
                    // and holding ws_mutex_ here would deadlock with feed()'s write.
                    beast::flat_buffer read_buf;
                    boost::system::error_code ec;
                    ws_->read(read_buf, ec);

                    if (ec == beast::error::timeout) {
                        continue;
                    }
                    if (ec) {
                        if (reader_running_) {
                            RCLCPP_DEBUG(logger_, "ServerAsrBackend: reader stopped: %s", ec.message().c_str());
                        }
                        connected_ = false;
                        break;
                    }

                    std::string msg = beast::buffers_to_string(read_buf.data());
                    AsrResult result = parse_response(msg);
                    if (result.ok()) {
                        std::lock_guard<std::mutex> lock(result_mutex_);
                        result_queue_.push(result);
                    }
                } catch (const std::exception& e) {
                    if (reader_running_) {
                        RCLCPP_DEBUG(logger_, "ServerAsrBackend: reader exception: %s", e.what());
                    }
                    connected_ = false;
                    break;
                }
            }
        });
    }

    void stop_reader() {
        reader_running_ = false;
        if (reader_thread_.joinable()) {
            reader_thread_.join();
        }
    }

    void shutdown() {
        reader_running_ = false;

        {
            std::lock_guard<std::mutex> lock(ws_mutex_);
            if (ws_ && connected_) {
                try {
                    std::string end_json = "{\"is_speaking\":false}";
                    ws_->text(true);
                    ws_->write(net::buffer(end_json));
                    ws_->close(websocket::close_code::normal);
                } catch (...) {}
            }
            connected_ = false;
        }

        stop_reader();
        ws_.reset();
        ioc_.reset();

        // Clear result queue
        std::lock_guard<std::mutex> lock(result_mutex_);
        std::queue<AsrResult>().swap(result_queue_);
    }

    AsrResult parse_response(const std::string& json) {
        AsrResult result;
        auto text_pos = json.find("\"text\"");
        if (text_pos == std::string::npos) return result;

        auto colon = json.find(':', text_pos);
        auto quote_start = json.find('"', colon + 1);
        auto quote_end = json.find('"', quote_start + 1);
        if (quote_start != std::string::npos && quote_end != std::string::npos) {
            result.text = json.substr(quote_start + 1, quote_end - quote_start - 1);
        }

        result.is_final = (json.find("\"is_final\":true") != std::string::npos) ||
                          (json.find("\"is_final\": true") != std::string::npos);

        if (!result.text.empty() && mode_ == "online") {
            result.is_final = true;
        }
        return result;
    }

    void parse_url(const std::string& url, std::string& host, std::string& port) {
        std::string stripped = url;
        if (stripped.substr(0, 5) == "ws://")
            stripped = stripped.substr(5);
        else if (stripped.substr(0, 6) == "wss://")
            stripped = stripped.substr(6);

        auto slash = stripped.find('/');
        if (slash != std::string::npos) stripped = stripped.substr(0, slash);

        auto colon = stripped.find(':');
        if (colon != std::string::npos) {
            host = stripped.substr(0, colon);
            port = stripped.substr(colon + 1);
        } else {
            host = stripped;
            port = "10095";
        }
    }

    rclcpp::Logger logger_{rclcpp::get_logger("server_asr")};
    std::string url_;
    std::string mode_ = "online";
    int sample_rate_ = 16000;
    std::atomic<bool> paused_{false};
    std::atomic<bool> connected_{false};
    std::unique_ptr<net::io_context> ioc_;
    std::unique_ptr<websocket::stream<beast::tcp_stream>> ws_;

    // Reader thread for async result reception
    std::thread reader_thread_;
    std::atomic<bool> reader_running_{false};
    std::mutex ws_mutex_;
    std::mutex result_mutex_;
    std::queue<AsrResult> result_queue_;
};

std::unique_ptr<AsrBackend> create_server_asr_backend() {
    return std::make_unique<ServerAsrBackend>();
}
