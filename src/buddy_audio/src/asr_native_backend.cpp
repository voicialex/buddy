#include "buddy_audio/asr_backend.hpp"
#include "buddy_audio/zipformer/pipeline.hpp"

#include <atomic>
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

        tokens_ = zipformer::Pipeline::load_tokens(opts.tokens_path);
        if (!pipeline_.init(opts)) {
            RCLCPP_ERROR(logger_, "Failed to init native zipformer pipeline");
            return false;
        }
        RCLCPP_INFO(logger_, "Native ASR backend initialized (runtime=%s)", config.runtime.c_str());
        return true;
    }

    AsrResult feed(const float* samples, int n) override {
        if (paused_.load()) return {};

        auto token_ids = pipeline_.accept_waveform(samples, n, false);
        if (token_ids.empty()) return {};

        std::ostringstream oss;
        for (auto id : token_ids) {
            if (id >= 0 && static_cast<size_t>(id) < tokens_.size()) {
                oss << tokens_[static_cast<size_t>(id)];
            }
        }

        AsrResult result;
        result.text = oss.str();
        result.is_final = false;
        return result;
    }

    void reset() override {
        pipeline_.reset();
    }

    void pause(bool paused) override {
        paused_.store(paused);
    }

private:
    zipformer::Pipeline pipeline_;
    std::vector<std::string> tokens_;
    rclcpp::Logger logger_{rclcpp::get_logger("native_asr")};
    std::atomic<bool> paused_{false};
};

std::unique_ptr<AsrBackend> create_native_asr_backend() {
    return std::make_unique<NativeAsrBackend>();
}
