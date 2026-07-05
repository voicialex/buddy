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

        if (n <= 0) return {};

        // Native zipformer path now buffers one whole utterance and only runs inference
        // when endpoint silence is detected.
        float sum = 0.0f;
        for (int i = 0; i < n; ++i) {
            sum += samples[i] * samples[i];
        }
        const float rms = std::sqrt(sum / static_cast<float>(n));
        if (rms < kSilenceRmsThreshold) {
            if (has_voice_) {
                ++silence_chunks_;
            }
        } else {
            if (!has_voice_) {
                pipeline_.reset();
                utterance_samples_.clear();
                RCLCPP_DEBUG(logger_, "[ASR_DIAG] utterance_start rms=%.5f", rms);
            }
            has_voice_ = true;
            silence_chunks_ = 0;
        }

        if (has_voice_) {
            utterance_samples_.insert(utterance_samples_.end(), samples, samples + n);
            RCLCPP_DEBUG(logger_,
                         "[ASR_DIAG] native_state n=%d rms=%.5f silence_chunks=%d/%d buffered_samples=%zu",
                        n,
                        rms,
                        silence_chunks_,
                        kSilenceChunksForFinal,
                        utterance_samples_.size());
        }

        if (!has_voice_ || silence_chunks_ < kSilenceChunksForFinal) return {};

        try {
            RCLCPP_DEBUG(logger_,
                         "[ASR_DIAG] final_flush begin buffered_samples=%zu silence_chunks=%d/%d",
                        utterance_samples_.size(),
                        silence_chunks_,
                        kSilenceChunksForFinal);
            const auto final_token_ids =
                pipeline_.accept_waveform(utterance_samples_.data(),
                                          static_cast<int>(utterance_samples_.size()),
                                          true);
            std::ostringstream id_oss;
            std::ostringstream text_oss;
            for (size_t i = 0; i < final_token_ids.size(); ++i) {
                if (i > 0) id_oss << ',';
                id_oss << final_token_ids[i];
            }
            for (auto id : final_token_ids) {
                if (id >= 0 && static_cast<size_t>(id) < tokens_.size()) {
                    text_oss << tokens_[static_cast<size_t>(id)];
                }
            }
            RCLCPP_INFO(logger_,
                        "[ASR_DIAG] final_flush token_ids=[%s] token_text=%s",
                        id_oss.str().c_str(),
                        text_oss.str().c_str());
            pending_text_ = text_oss.str();
        } catch (const std::exception& e) {
            RCLCPP_ERROR(logger_, "Native ASR final flush failed: %s", e.what());
        }

        AsrResult result;
        result.text = pending_text_;
        result.is_final = !result.text.empty();
        if (result.is_final) {
            RCLCPP_DEBUG(logger_, "[ASR_DIAG] final_result=%s", result.text.c_str());
        } else {
            RCLCPP_DEBUG(logger_, "[ASR_DIAG] final_result empty");
        }
        pending_text_.clear();
        utterance_samples_.clear();
        silence_chunks_ = 0;
        has_voice_ = false;
        pipeline_.reset();
        return result;
    }

    void reset() override {
        pipeline_.reset();
        pending_text_.clear();
        utterance_samples_.clear();
        silence_chunks_ = 0;
        has_voice_ = false;
    }

    void pause(bool paused) override {
        paused_.store(paused);
        if (paused) {
            pending_text_.clear();
            utterance_samples_.clear();
            silence_chunks_ = 0;
            has_voice_ = false;
        }
    }

private:
    static constexpr float kSilenceRmsThreshold = 0.01f;
    static constexpr int kSilenceChunksForFinal = 9;  // 9 * 100ms = 900ms

    zipformer::Pipeline pipeline_;
    std::vector<std::string> tokens_;
    std::string pending_text_;
    std::vector<float> utterance_samples_;
    int silence_chunks_ = 0;
    bool has_voice_ = false;
    rclcpp::Logger logger_{rclcpp::get_logger("native_asr")};
    std::atomic<bool> paused_{false};
};

std::unique_ptr<AsrBackend> create_native_asr_backend() {
    return std::make_unique<NativeAsrBackend>();
}
