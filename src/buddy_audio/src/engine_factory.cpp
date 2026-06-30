#include "buddy_audio/engine_factory.hpp"

#include <exception>

bool create_asr_engine(const AudioConfig& cfg, rclcpp::Logger logger, AsrEngineBundle* out) {
    if (!out) {
        return false;
    }

    const std::string mode = cfg.asr_mode;
    const std::string engine = cfg.asr_engine;
    const std::string runtime = cfg.asr_runtime;

    auto backend = create_asr_backend(mode, engine, runtime);
    if (!backend) {
        RCLCPP_ERROR(logger, "Unknown ASR mode=%s engine=%s", mode.c_str(), engine.c_str());
        return false;
    }

    AsrBackendConfig config{};
    if (mode == "local" && engine == "zipformer-rknn") {
        config = cfg.asr_local_native;
    } else if (mode == "local") {
        config = cfg.asr_local_sherpa;
    } else {
        config = cfg.asr_server;
    }
    config.runtime = runtime;

    bool initialized = false;
    try {
        initialized = backend->initialize(config, logger);
    } catch (const std::exception& e) {
        RCLCPP_ERROR(logger, "ASR initialize exception: %s", e.what());
        initialized = false;
    }

    if (!initialized && mode == "local" && runtime == "rknnruntime") {
        RCLCPP_WARN(logger, "ASR RKNN init failed, fallback to CPU (engine=sherpa-onnx, runtime=onnxruntime)");
        const std::string fallback_engine = "sherpa-onnx";
        const std::string fallback_runtime = "onnxruntime";

        auto fallback = create_asr_backend(mode, fallback_engine, fallback_runtime);
        if (!fallback) {
            RCLCPP_ERROR(logger, "Failed to create fallback ASR backend");
            return false;
        }
        AsrBackendConfig fallback_cfg = cfg.asr_local_sherpa;
        fallback_cfg.runtime = fallback_runtime;
        try {
            initialized = fallback->initialize(fallback_cfg, logger);
        } catch (const std::exception& e) {
            RCLCPP_ERROR(logger, "ASR fallback initialize exception: %s", e.what());
            initialized = false;
        }
        if (!initialized) {
            return false;
        }
        out->backend = std::move(fallback);
        out->mode = mode;
        out->engine = fallback_engine;
        out->runtime = fallback_runtime;
        return true;
    }

    if (!initialized) {
        return false;
    }
    out->backend = std::move(backend);
    out->mode = mode;
    out->engine = engine;
    out->runtime = runtime;
    return true;
}

bool create_tts_engine(
    const AudioConfig& cfg,
    rclcpp::Logger logger,
    std::unique_ptr<TtsBackend>* out_backend,
    std::string* out_mode,
    std::string* out_engine,
    std::string* out_runtime) {
    if (!out_backend || !out_mode || !out_engine || !out_runtime) {
        return false;
    }

    const std::string mode = cfg.tts_mode;
    const std::string engine = cfg.tts_engine;
    const std::string runtime = cfg.tts_runtime;

    auto backend = create_tts_backend(mode, engine, runtime);
    if (!backend) {
        return false;
    }

    TtsBackendConfig config{};
    if (mode == "server") {
        config = cfg.tts_server;
    } else if (engine == "sherpa-onnx") {
        config = cfg.tts_local_sherpa;
    } else if (engine == "melo-rknn") {
        config = cfg.tts_local_melo;
    } else if (engine == "moss-onnx") {
        config = cfg.tts_local_moss;
    } else {
        RCLCPP_WARN(logger, "Unknown TTS engine=%s, defaulting to sherpa config", engine.c_str());
        config = cfg.tts_local_sherpa;
    }
    config.runtime = runtime;

    bool initialized = false;
    try {
        initialized = backend->initialize(config, logger);
    } catch (const std::exception& e) {
        RCLCPP_ERROR(logger, "TTS initialize exception: %s", e.what());
        initialized = false;
    }

    if (!initialized) {
        return false;
    }

    *out_backend = std::move(backend);
    *out_mode = mode;
    *out_engine = engine;
    *out_runtime = runtime;
    return true;
}
