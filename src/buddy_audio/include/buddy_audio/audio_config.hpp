#pragma once

#include <string>

#include <rclcpp_lifecycle/lifecycle_node.hpp>

#include "buddy_audio/speech_recognizer.hpp"
#include "buddy_audio/asr/asr_backend.hpp"
#include "buddy_audio/io/audio_preprocessor.hpp"
#include "buddy_audio/tts/tts_backend.hpp"

struct AudioConfig {
    std::string models_dir;
    std::string mic_device;
    std::string speaker_device;
    int sample_rate = 16000;
    AudioPreprocessConfig preprocess{};
    bool kws_enabled = true;
    int asr_cooldown_chunks = 5;
    int asr_wake_guard_chunks = 7;

    KwsConfig kws{};

    std::string asr_mode = "local";
    std::string asr_engine = "sherpa-onnx";
    std::string asr_runtime = "onnxruntime";
    AsrBackendConfig asr_local_native{};
    AsrBackendConfig asr_local_sherpa{};
    AsrBackendConfig asr_server{};

    std::string tts_mode = "local";
    std::string tts_engine = "sherpa-onnx";
    std::string tts_runtime = "onnxruntime";
    TtsBackendConfig tts_local_sherpa{};
    TtsBackendConfig tts_local_melo{};
    TtsBackendConfig tts_local_moss{};
    TtsBackendConfig tts_server{};
};

void declare_audio_parameters(rclcpp_lifecycle::LifecycleNode& node);
AudioConfig load_audio_config(rclcpp_lifecycle::LifecycleNode& node);
