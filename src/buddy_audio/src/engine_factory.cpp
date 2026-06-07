#include "buddy_audio/engine_factory.hpp"

#include <dlfcn.h>
#include <exception>

namespace {

std::string resolve_asr_runtime(const std::string& requested, rclcpp::Logger logger) {
    if (requested != "auto") {
        return requested;
    }
#ifdef HAS_RKNN
    void* rknn_handle = dlopen("librknnrt.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!rknn_handle) {
        rknn_handle = dlopen("librknnrt.so", RTLD_LAZY);
    }
    if (rknn_handle) {
        dlclose(rknn_handle);
        RCLCPP_INFO(logger, "RKNN runtime detected, using NPU for ASR");
        return "rknnruntime";
    }
#endif
    (void)logger;
    return "onnxruntime";
}

std::string resolve_tts_runtime(const std::string& requested, const std::string& engine) {
    if (requested != "auto") {
        return requested;
    }
#ifdef HAS_RKNN
    if (engine == "native" || engine == "melo-rknn") {
        void* rknn_handle = dlopen("librknnrt.so", RTLD_LAZY | RTLD_NOLOAD);
        if (!rknn_handle) {
            rknn_handle = dlopen("librknnrt.so", RTLD_LAZY);
        }
        if (rknn_handle) {
            dlclose(rknn_handle);
            return "rknnruntime";
        }
    }
#else
    (void)engine;
#endif
    return "onnxruntime";
}

std::string normalize_asr_engine(std::string engine, const std::string& runtime) {
    if (engine == "auto") {
        return (runtime == "rknnruntime") ? "zipformer-rknn" : "sherpa-onnx";
    }
    if (engine == "native") {
        return "zipformer-rknn";
    }
    if (engine == "sherpa") {
        return "sherpa-onnx";
    }
    return engine;
}

std::string normalize_tts_engine(std::string engine, const std::string& runtime) {
    if (engine == "auto") {
        return (runtime == "rknnruntime") ? "melo-rknn" : "sherpa-onnx";
    }
    if (engine == "sherpa") {
        return "sherpa-onnx";
    }
    if (engine == "native") {
        return (runtime == "rknnruntime") ? "melo-rknn" : "moss-onnx";
    }
    if (engine == "moss") {
        return "moss-onnx";
    }
    return engine;
}

}  // namespace

bool create_asr_engine(const AudioConfig& cfg, rclcpp::Logger logger, AsrEngineBundle* out) {
    if (!out) {
        return false;
    }

    const std::string mode = cfg.asr_mode;
    const std::string runtime = resolve_asr_runtime(cfg.asr_runtime, logger);
    const std::string engine = normalize_asr_engine(cfg.asr_engine, runtime);

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
    std::string engine = cfg.tts_engine;
    const std::string runtime = resolve_tts_runtime(cfg.tts_runtime, engine);
    engine = normalize_tts_engine(engine, runtime);

    auto backend = create_tts_backend(mode, engine, runtime);
    if (!backend) {
        return false;
    }

    TtsBackendConfig config{};
    if (mode == "server") {
        config = cfg.tts_server;
    } else if (engine == "sherpa-onnx" || engine == "sherpa") {
        config = cfg.tts_local_sherpa;
    } else if (engine == "melo-rknn") {
        config = cfg.tts_local_melo;
    } else if (engine == "moss-onnx" || engine == "moss" || engine == "native") {
        config = cfg.tts_local_moss;
    } else {
        config = cfg.tts_local_sherpa;
    }
    config.runtime = runtime;

    if (!backend->initialize(config, logger)) {
        return false;
    }

    *out_backend = std::move(backend);
    *out_mode = mode;
    *out_engine = engine;
    *out_runtime = runtime;
    return true;
}
