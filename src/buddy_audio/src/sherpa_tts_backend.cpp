#include <sherpa-onnx/c-api/c-api.h>

#include <cstring>
#include <rclcpp/rclcpp.hpp>

#include "buddy_audio/tts_backend.hpp"

class SherpaTtsBackend : public TtsBackend {
public:
    ~SherpaTtsBackend() override {
        if (tts_) {
            SherpaOnnxDestroyOfflineTts(tts_);
        }
    }

    bool initialize(const TtsBackendConfig& config, rclcpp::Logger logger) override {
        if (config.model.empty()) {
            RCLCPP_WARN(logger, "SherpaTtsBackend: model path is empty");
            return false;
        }

        sid_ = config.sid;
        speed_ = config.speed;

        SherpaOnnxOfflineTtsConfig cfg{};
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.model.vits.model = config.model.c_str();
        cfg.model.vits.tokens = config.tokens.c_str();
        if (!config.lexicon.empty()) {
            cfg.model.vits.lexicon = config.lexicon.c_str();
        }
        if (!config.data_dir.empty()) {
            cfg.model.vits.data_dir = config.data_dir.c_str();
        }
        if (!config.rule_fsts.empty()) {
            cfg.rule_fsts = config.rule_fsts.c_str();
        }
        cfg.model.num_threads = 1;
        cfg.model.provider = "cpu";

        tts_ = const_cast<SherpaOnnxOfflineTts*>(SherpaOnnxCreateOfflineTts(&cfg));
        if (!tts_) {
            RCLCPP_ERROR(logger, "SherpaTtsBackend: failed to create TTS");
            return false;
        }

        RCLCPP_INFO(logger,
                    "SherpaTtsBackend ready (sample_rate=%d, speakers=%d)",
                    SherpaOnnxOfflineTtsSampleRate(tts_),
                    SherpaOnnxOfflineTtsNumSpeakers(tts_));
        return true;
    }

    TtsResult generate(const std::string& text) override {
        TtsResult result;
        SherpaOnnxGenerationConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.sid = sid_;
        cfg.speed = speed_;
        auto* audio = SherpaOnnxOfflineTtsGenerateWithConfig(tts_, text.c_str(), &cfg, nullptr, nullptr);
        if (audio && audio->samples && audio->n > 0) {
            result.samples.assign(audio->samples, audio->samples + audio->n);
            result.sample_rate = audio->sample_rate;
        }
        if (audio) {
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        }
        return result;
    }

private:
    SherpaOnnxOfflineTts* tts_ = nullptr;
    int sid_ = 0;
    float speed_ = 1.0f;
};

std::unique_ptr<TtsBackend> create_sherpa_tts_backend() {
    return std::make_unique<SherpaTtsBackend>();
}
