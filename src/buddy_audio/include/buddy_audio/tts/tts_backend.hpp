#pragma once
#include <cstdint>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <vector>

struct TtsResult {
    std::vector<float> samples;  // PCM float32 mono
    int32_t sample_rate = 0;
    bool ok() const { return !samples.empty() && sample_rate > 0; }
};

struct TtsBackendConfig {
    std::string models_dir;
    std::string model_dir;
    // Sherpa-specific
    std::string model_type = "kokoro";  // kokoro | vits | melo
    std::string model;
    std::string tokens;
    std::string lexicon;
    std::string data_dir;
    std::string voices;    // Kokoro voices.bin
    std::string lang;      // Kokoro language hint (e.g. "zh")
    std::string dict_dir;  // jieba dict dir for Chinese
    std::string rule_fsts;
    int sid = 0;
    float speed = 1.0f;
    // HTTP-specific
    std::string server_url;
    // MOSS-specific
    std::string voice;
    int max_new_frames = 375;
    int seed = 1234;
    // Melo RKNN-specific
    std::string melo_speaker;
    std::string melo_rank = "rank3";         // rank1 | rank2 | rank3
    std::string melo_bert_mode = "rknn";     // zero | rknn
    float melo_speed = 1.0f;
    float melo_sdp_ratio = 0.0f;
    float melo_noise_scale = 0.6f;
    float melo_noise_scale_w = 0.8f;
    // Common
    std::string runtime = "onnxruntime";
    int sample_rate = 16000;
};

class TtsBackend {
public:
    virtual ~TtsBackend() = default;
    virtual bool initialize(const TtsBackendConfig& config, rclcpp::Logger logger) = 0;
    virtual TtsResult generate(const std::string& text) = 0;
};

std::unique_ptr<TtsBackend> create_sherpa_tts_backend();
std::unique_ptr<TtsBackend> create_http_tts_backend();
std::unique_ptr<TtsBackend> create_moss_tts_backend();
std::unique_ptr<TtsBackend> create_melo_rknn_tts_backend();

inline std::unique_ptr<TtsBackend> create_tts_backend(
    const std::string& mode, const std::string& engine, const std::string& runtime = "onnxruntime") {
    (void)runtime;  // reserved for future backend selection
    if (mode == "server") return create_http_tts_backend();
    if (engine == "melo-rknn") {
        return create_melo_rknn_tts_backend();
    }
    if (engine == "moss" || engine == "moss-onnx") return create_moss_tts_backend();
    return create_sherpa_tts_backend();
}
