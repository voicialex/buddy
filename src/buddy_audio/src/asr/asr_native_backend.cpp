#include "buddy_audio/asr/asr_backend.hpp"
#include "buddy_audio/zipformer/pipeline.hpp"

#include <atomic>
#include <cmath>
#include <sstream>

class NativeAsrBackend : public AsrBackend {
public:
    bool initialize(const AsrBackendConfig& config, rclcpp::Logger logger) override {
        logger_ = logger;

        zipformer::PipelineOptions opts;
        std::string base = config.models_dir + "/" + config.model_dir;
        opts.encoder_model = base + "/" + config.encoder;
        opts.decoder_model = base + "/" + config.decoder;
        opts.joiner_model = base + "/" + config.joiner;
        opts.tokens_path = base + "/" + config.tokens;
        opts.runtime = config.runtime;
        opts.sample_rate = config.sample_rate;
        RCLCPP_INFO(
            logger_,
            "Native ASR loading models: encoder=%s decoder=%s joiner=%s tokens=%s runtime=%s",
            opts.encoder_model.c_str(),
            opts.decoder_model.c_str(),
            opts.joiner_model.c_str(),
            opts.tokens_path.c_str(),
            opts.runtime.c_str());
        try {
            tokens_ = zipformer::Pipeline::load_tokens(opts.tokens_path);
            if (!pipeline_.init(opts)) {
                RCLCPP_ERROR(logger_, "Failed to init native zipformer pipeline");
                return false;
            }
        } catch (const std::exception& e) {
            RCLCPP_ERROR(logger_, "Native ASR model load/init failed: %s", e.what());
            return false;
        }
        RCLCPP_INFO(logger_, "Native ASR backend initialized (runtime=%s)", config.runtime.c_str());
        return true;
    }

    AsrResult feed(const float* samples, int n) override {
        if (paused_.load()) return {};

        std::vector<int64_t> token_ids;
        try {
            token_ids = pipeline_.accept_waveform(samples, n, false);
        } catch (const std::exception& e) {
            RCLCPP_ERROR(logger_, "Native ASR inference failed: %s", e.what());
            return {};
        }
        if (!token_ids.empty()) {
            std::ostringstream oss;
            for (auto id : token_ids) {
                if (id >= 0 && static_cast<size_t>(id) < tokens_.size()) {
                    oss << tokens_[static_cast<size_t>(id)];
                }
            }
            const std::string chunk = oss.str();
            if (!chunk.empty()) {
                pending_text_ += chunk;
            }
            silence_chunks_ = 0;
            return {};
        }

        if (pending_text_.empty() || n <= 0) return {};

        // Native zipformer path has no built-in endpoint flag. Use short silence to close one utterance.
        float sum = 0.0f;
        for (int i = 0; i < n; ++i) {
            sum += samples[i] * samples[i];
        }
        const float rms = std::sqrt(sum / static_cast<float>(n));
        if (rms < kSilenceRmsThreshold) {
            ++silence_chunks_;
        } else {
            silence_chunks_ = 0;
        }

        if (silence_chunks_ < kSilenceChunksForFinal) return {};

        AsrResult result;
        result.text = pending_text_;
        result.is_final = true;
        pending_text_.clear();
        silence_chunks_ = 0;
        pipeline_.reset();
        return result;
    }

    void reset() override {
        pipeline_.reset();
        pending_text_.clear();
        silence_chunks_ = 0;
    }

    void pause(bool paused) override {
        paused_.store(paused);
        if (paused) {
            pending_text_.clear();
            silence_chunks_ = 0;
        }
    }

private:
    static constexpr float kSilenceRmsThreshold = 0.01f;
    static constexpr int kSilenceChunksForFinal = 4;  // 4 * 100ms = 400ms

    zipformer::Pipeline pipeline_;
    std::vector<std::string> tokens_;
    std::string pending_text_;
    int silence_chunks_ = 0;
    rclcpp::Logger logger_{rclcpp::get_logger("native_asr")};
    std::atomic<bool> paused_{false};
};

std::unique_ptr<AsrBackend> create_native_asr_backend() {
    return std::make_unique<NativeAsrBackend>();
}
