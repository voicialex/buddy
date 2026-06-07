#include <curl/curl.h>

#include <cstring>
#include <rclcpp/rclcpp.hpp>

#include "buddy_audio/tts/tts_backend.hpp"

namespace {

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    buf->append(ptr, total);
    return total;
}

size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* sample_rate = static_cast<int32_t*>(userdata);
    size_t total = size * nitems;
    std::string line(buffer, total);
    const std::string prefix = "X-Sample-Rate: ";
    if (line.substr(0, prefix.size()) == prefix) {
        try {
            *sample_rate = std::stoi(line.substr(prefix.size()));
        } catch (...) {}
    }
    return total;
}

}  // namespace

class HttpTtsBackend : public TtsBackend {
public:
    bool initialize(const TtsBackendConfig& config, rclcpp::Logger logger) override {
        url_ = config.server_url;
        if (url_.empty()) {
            RCLCPP_ERROR(logger, "HttpTtsBackend: server_url is empty");
            return false;
        }
        RCLCPP_INFO(logger, "HttpTtsBackend ready (url=%s)", url_.c_str());
        return true;
    }

    TtsResult generate(const std::string& text) override {
        TtsResult result;

        CURL* curl = curl_easy_init();
        if (!curl) {
            return result;
        }

        std::string json = "{\"text\":\"";
        for (char c : text) {
            if (c == '"') {
                json += "\\\"";
            } else if (c == '\\') {
                json += "\\\\";
            } else {
                json += c;
            }
        }
        json += "\"}";

        std::string response_body;
        int32_t sample_rate = 16000;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &sample_rate);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK || response_body.empty()) {
            return result;
        }

        size_t n_samples = response_body.size() / sizeof(float);
        if (n_samples == 0) {
            return result;
        }

        result.samples.resize(n_samples);
        std::memcpy(result.samples.data(), response_body.data(), n_samples * sizeof(float));
        result.sample_rate = sample_rate;
        return result;
    }

private:
    std::string url_;
};

std::unique_ptr<TtsBackend> create_http_tts_backend() {
    return std::make_unique<HttpTtsBackend>();
}
