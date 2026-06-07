#pragma once

#include <deque>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <vector>

struct AudioPreprocessConfig {
    bool enable = false;
    bool vad_enable = false;
    bool vad_gate_enable = false;
    bool vad_gate_with_kws = false;
    int vad_likelihood = 2;          // 0..3 (very-low .. high)
    int vad_frame_ms = 10;           // 10/20/30
    int vad_min_voice_frames = 1;    // >=1
    int vad_hangover_frames = 12;    // frames at 10ms unit

    bool ns_enable = false;
    int ns_level = 2;  // 0..3 (low..very_high)

    bool agc_enable = false;
    int agc_mode = 2;                // 0:adaptive_analog 1:adaptive_digital 2:fixed_digital
    int agc_target_level_dbfs = 3;   // 0..31
    int agc_compression_gain_db = 9; // 0..90
    bool agc_limiter_enable = true;
    int agc_analog_level_min = 0;
    int agc_analog_level_max = 255;
    int agc_stream_analog_level = 127;

    bool aec_enable = false;
    int aec_suppression_level = 1;  // 0..2 (low/moderate/high)
    int aec_stream_delay_ms = 60;
};

struct AudioPreprocessResult {
    bool has_voice = true;
    bool should_pass = true;
};

class AudioPreprocessor {
public:
    AudioPreprocessor(rclcpp::Logger logger, int sample_rate, const AudioPreprocessConfig& cfg);
    ~AudioPreprocessor();

    bool is_enabled() const { return enabled_; }

    // Mic path: process in-place and return VAD gate decision.
    AudioPreprocessResult process_capture_chunk(std::vector<float>* chunk, bool kws_enabled);

    // Speaker path: feed far-end render samples for AEC reference.
    void push_render_chunk(const float* samples, int n, int sample_rate);

private:
    void resample_and_append_render_locked(const float* samples, int n, int sample_rate);

    rclcpp::Logger logger_;
    int sample_rate_ = 16000;
    int frame_samples_ = 160;  // 10ms at 16k
    AudioPreprocessConfig cfg_{};

    bool enabled_ = false;
    void* webrtc_state_ = nullptr;

    std::mutex mu_;
    std::deque<float> render_queue_;
    int vad_hangover_remaining_frames_ = 0;
};
