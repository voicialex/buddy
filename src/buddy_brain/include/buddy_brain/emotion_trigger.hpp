#pragma once

#include <chrono>
#include <string>
#include <vector>

/// Detects when a negative emotion has persisted long enough to trigger
/// proactive interaction.
class EmotionTrigger {
public:
    struct Config {
        std::vector<std::string> negative_emotions{"sad", "angry", "fear"};
        float confidence_threshold{0.7f};
        double duration_seconds{3.0};
        double cooldown_seconds{60.0};
    };

    explicit EmotionTrigger(const Config& cfg);

    /// Update with latest emotion reading. Returns true if trigger should fire.
    bool update(const std::string& emotion, float confidence);

    void reset();

private:
    Config cfg_;
    bool tracking_{false};
    std::chrono::steady_clock::time_point negative_since_;
    std::chrono::steady_clock::time_point last_trigger_;
};
