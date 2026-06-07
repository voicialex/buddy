#pragma once
#include <atomic>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <string>

struct AsrResult {
    std::string text;
    bool is_final = false;
    bool ok() const { return !text.empty() && is_final; }
};

struct AsrBackendConfig {
    std::string models_dir;
    std::string model_dir;
    std::string encoder;
    std::string decoder;
    std::string joiner;
    std::string tokens;
    std::string decoding_method = "greedy_search";
    std::string server_url;
    std::string server_mode = "online";
    std::string runtime = "onnxruntime";
    int sample_rate = 16000;
};

class AsrBackend {
public:
    virtual ~AsrBackend() = default;
    virtual bool initialize(const AsrBackendConfig& config, rclcpp::Logger logger) = 0;
    virtual AsrResult feed(const float* samples, int n) = 0;
    virtual void reset() = 0;
    virtual void pause(bool paused) = 0;
};

std::unique_ptr<AsrBackend> create_sherpa_asr_backend();
std::unique_ptr<AsrBackend> create_server_asr_backend();
#if HAS_RKNN
std::unique_ptr<AsrBackend> create_native_asr_backend();
#endif

inline std::unique_ptr<AsrBackend> create_asr_backend(
    const std::string& mode, const std::string& engine, const std::string& runtime = "onnxruntime") {
    (void)runtime;  // used via AsrBackendConfig
    (void)engine;   // only used when HAS_RKNN is enabled
    if (mode == "server") return create_server_asr_backend();
#if HAS_RKNN
    if (engine == "native" || engine == "zipformer-rknn") return create_native_asr_backend();
#endif
    return create_sherpa_asr_backend();
}
