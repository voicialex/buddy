#include <sherpa-onnx/c-api/c-api.h>

#include <atomic>
#include <cmath>
#include <filesystem>

#include "buddy_audio/asr/asr_backend.hpp"

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
        cfg.enable_endpoint = 0;  // use our own VAD-based endpoint detection

        asr_ = const_cast<SherpaOnnxOnlineRecognizer*>(SherpaOnnxCreateOnlineRecognizer(&cfg));
        if (!asr_) {
            RCLCPP_ERROR(logger_, "LocalAsrBackend: failed to create recognizer");
            return false;
        }
        asr_stream_ = const_cast<SherpaOnnxOnlineStream*>(SherpaOnnxCreateOnlineStream(asr_));
        RCLCPP_INFO(logger_, "LocalAsrBackend ready (custom VAD endpoint, threshold=%.4f)", kSilenceRmsThreshold);
        return true;
    }

    AsrResult feed(const float* samples, int n) override {
        AsrResult result;
        if (paused_ || !asr_stream_) return result;

        // RMS-based voice/silence detection
        double sum_sq = 0.0;
        for (int i = 0; i < n; ++i) {
            sum_sq += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
        }
        const float rms = static_cast<float>(std::sqrt(sum_sq / static_cast<double>(n)));

        if (rms < kSilenceRmsThreshold) {
            if (has_voice_) {
                ++silence_chunks_;
            }
        } else {
            if (!has_voice_) {
                RCLCPP_INFO(logger_, "ASR voice onset rms=%.4f", rms);
            }
            has_voice_ = true;
            silence_chunks_ = 0;
        }

        // Always feed audio to sherpa-onnx for streaming processing
        SherpaOnnxOnlineStreamAcceptWaveform(asr_stream_, sample_rate_, samples, n);

        // Decode available frames
        int ready_count = 0;
        while (SherpaOnnxIsOnlineStreamReady(asr_, asr_stream_)) {
            SherpaOnnxDecodeOnlineStream(asr_, asr_stream_);
            ++ready_count;
        }

        // Check for endpoint: voice followed by silence
        if (has_voice_ && silence_chunks_ >= kSilenceChunksForFinal) {
            // Finalize utterance
            SherpaOnnxOnlineStreamInputFinished(asr_stream_);
            while (SherpaOnnxIsOnlineStreamReady(asr_, asr_stream_)) {
                SherpaOnnxDecodeOnlineStream(asr_, asr_stream_);
            }

            const SherpaOnnxOnlineRecognizerResult* r = SherpaOnnxGetOnlineStreamResult(asr_, asr_stream_);
            if (r && r->text && r->text[0] != '\0') {
                result.text = r->text;
                result.is_final = true;
            }
            if (r) SherpaOnnxDestroyOnlineRecognizerResult(r);

            RCLCPP_INFO(logger_, "ASR endpoint: text='%s' voice_chunks=%d silence_chunks=%d",
                        result.text.c_str(), voice_chunk_count_, silence_chunks_);

            SherpaOnnxOnlineStreamReset(asr_, asr_stream_);
            has_voice_ = false;
            silence_chunks_ = 0;
            voice_chunk_count_ = 0;
        } else if (has_voice_) {
            ++voice_chunk_count_;
        }

        return result;
    }

    void reset() override {
        if (asr_ && asr_stream_) {
            SherpaOnnxOnlineStreamReset(asr_, asr_stream_);
            has_voice_ = false;
            silence_chunks_ = 0;
            voice_chunk_count_ = 0;
        }
    }

    void pause(bool paused) override {
        paused_ = paused;
        if (paused) {
            has_voice_ = false;
            silence_chunks_ = 0;
            voice_chunk_count_ = 0;
        }
    }

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

    static constexpr float kSilenceRmsThreshold = 0.002f;
    static constexpr int kSilenceChunksForFinal = 9;  // 9 × 100ms = 900ms

    rclcpp::Logger logger_{rclcpp::get_logger("local_asr")};
    int sample_rate_ = 16000;
    std::atomic<bool> paused_{false};
    SherpaOnnxOnlineRecognizer* asr_ = nullptr;
    SherpaOnnxOnlineStream* asr_stream_ = nullptr;

    // Custom VAD state
    bool has_voice_ = false;
    int silence_chunks_ = 0;
    int voice_chunk_count_ = 0;
};

std::unique_ptr<AsrBackend> create_sherpa_asr_backend() {
    return std::make_unique<LocalAsrBackend>();
}
