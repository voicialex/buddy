#pragma once

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "buddy_audio/audio_config.hpp"
#include "buddy_audio/asr/asr_backend.hpp"
#include "buddy_audio/tts/tts_backend.hpp"

struct AsrEngineBundle {
    std::unique_ptr<AsrBackend> backend;
    std::string mode;
    std::string engine;
    std::string runtime;
};

bool create_asr_engine(const AudioConfig& cfg, rclcpp::Logger logger, AsrEngineBundle* out);
bool create_tts_engine(
    const AudioConfig& cfg,
    rclcpp::Logger logger,
    std::unique_ptr<TtsBackend>* out_backend,
    std::string* out_mode,
    std::string* out_engine,
    std::string* out_runtime);
