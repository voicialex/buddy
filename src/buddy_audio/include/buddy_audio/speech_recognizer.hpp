#pragma once
#include <sherpa-onnx/c-api/c-api.h>

#include <atomic>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <string>

#include "buddy_audio/asr_backend.hpp"

struct RecognitionEvent {
    enum Type { NONE, WAKE_WORD, ASR_TEXT };
    Type type = NONE;
    std::string text;
};

struct KwsConfig {
    std::string encoder;
    std::string decoder;
    std::string joiner;
    std::string tokens;
    std::string keywords_file;
    float threshold = 0.1f;
    float score = 3.0f;
};

class SpeechRecognizer {
public:
    explicit SpeechRecognizer(rclcpp::Logger logger);
    ~SpeechRecognizer();

    bool configure(int sample_rate,
                   bool kws_enabled,
                   const KwsConfig& kws_cfg,
                   std::unique_ptr<AsrBackend> asr_backend);
    RecognitionEvent feed(const float* samples, int n);
    void reset_asr();
    void pause_asr(bool paused);
    void cleanup();

private:
    template <typename T>
    static T* sherpa_cast(const T* p) {
        return const_cast<T*>(p);
    }

    static void setup_transducer(SherpaOnnxOnlineTransducerModelConfig& trans,
                                 SherpaOnnxOnlineModelConfig& model,
                                 SherpaOnnxFeatureConfig& feat,
                                 int sample_rate,
                                 const std::string& encoder,
                                 const std::string& decoder,
                                 const std::string& joiner,
                                 const std::string& tokens);

    rclcpp::Logger logger_;
    int sample_rate_ = 16000;
    bool kws_enabled_ = true;

    SherpaOnnxKeywordSpotter* kws_ = nullptr;
    SherpaOnnxOnlineStream* kws_stream_ = nullptr;

    std::unique_ptr<AsrBackend> asr_backend_;
};