#include <sherpa-onnx/c-api/c-api.h>

#include <atomic>
#include <filesystem>

#include "buddy_audio/asr_backend.hpp"

class LocalAsrBackend : public AsrBackend {
public:
    ~LocalAsrBackend() override { cleanup(); }

    bool initialize(const AsrBackendConfig& config, rclcpp::Logger logger) override {
        logger_ = logger;
        sample_rate_ = config.sample_rate;

        auto resolve = [&](const std::string& filename) -> std::string {
            if (filename.empty() || filename[0] == '/') return filename;
            return (std::filesystem::path(config.models_dir) / config.model_dir / filename).string();
        };

        std::string encoder = resolve(config.encoder);
        std::string decoder = resolve(config.decoder);
        std::string joiner = resolve(config.joiner);
        std::string tokens = resolve(config.tokens);

        SherpaOnnxOnlineRecognizerConfig cfg{};
        cfg.feat_config.sample_rate = sample_rate_;
        cfg.feat_config.feature_dim = 80;
        cfg.model_config.transducer.encoder = encoder.c_str();
        cfg.model_config.transducer.decoder = decoder.c_str();
        cfg.model_config.transducer.joiner = joiner.c_str();
        cfg.model_config.tokens = tokens.c_str();
        cfg.model_config.provider = "cpu";
        cfg.model_config.num_threads = 1;
        cfg.decoding_method = config.decoding_method.c_str();
        cfg.enable_endpoint = 1;
        cfg.rule1_min_trailing_silence = 2.4f;
        cfg.rule2_min_trailing_silence = 1.2f;
        cfg.rule3_min_utterance_length = 20.0f;

        asr_ = const_cast<SherpaOnnxOnlineRecognizer*>(SherpaOnnxCreateOnlineRecognizer(&cfg));
        if (!asr_) {
            RCLCPP_ERROR(logger_, "LocalAsrBackend: failed to create recognizer");
            return false;
        }
        asr_stream_ = const_cast<SherpaOnnxOnlineStream*>(SherpaOnnxCreateOnlineStream(asr_));
        RCLCPP_INFO(logger_, "LocalAsrBackend ready");
        return true;
    }

    AsrResult feed(const float* samples, int n) override {
        AsrResult result;
        if (paused_ || !asr_stream_) return result;

        SherpaOnnxOnlineStreamAcceptWaveform(asr_stream_, sample_rate_, samples, n);
        while (SherpaOnnxIsOnlineStreamReady(asr_, asr_stream_)) {
            SherpaOnnxDecodeOnlineStream(asr_, asr_stream_);
        }
        if (SherpaOnnxOnlineStreamIsEndpoint(asr_, asr_stream_)) {
            const SherpaOnnxOnlineRecognizerResult* r = SherpaOnnxGetOnlineStreamResult(asr_, asr_stream_);
            if (r && r->text && r->text[0] != '\0') {
                result.text = r->text;
                result.is_final = true;
            }
            if (r) SherpaOnnxDestroyOnlineRecognizerResult(r);
            SherpaOnnxOnlineStreamReset(asr_, asr_stream_);
        }
        return result;
    }

    void reset() override {
        if (asr_ && asr_stream_) {
            SherpaOnnxOnlineStreamReset(asr_, asr_stream_);
        }
    }

    void pause(bool paused) override { paused_ = paused; }

private:
    void cleanup() {
        if (asr_stream_) {
            SherpaOnnxDestroyOnlineStream(asr_stream_);
            asr_stream_ = nullptr;
        }
        if (asr_) {
            SherpaOnnxDestroyOnlineRecognizer(asr_);
            asr_ = nullptr;
        }
    }

    rclcpp::Logger logger_{rclcpp::get_logger("local_asr")};
    int sample_rate_ = 16000;
    std::atomic<bool> paused_{false};
    SherpaOnnxOnlineRecognizer* asr_ = nullptr;
    SherpaOnnxOnlineStream* asr_stream_ = nullptr;
};

std::unique_ptr<AsrBackend> create_sherpa_asr_backend() {
    return std::make_unique<LocalAsrBackend>();
}
