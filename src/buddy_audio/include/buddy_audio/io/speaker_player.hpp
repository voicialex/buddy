#pragma once
#include <alsa/asoundlib.h>

#include <atomic>
#include <buddy_interfaces/msg/sentence.hpp>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <thread>

#include "buddy_audio/tts/tts_backend.hpp"

class TtsPlayer {
public:
    explicit TtsPlayer(rclcpp::Logger logger);
    ~TtsPlayer();

    using DoneCallback = std::function<void()>;
    using RenderTapCallback = std::function<void(const float* samples, int32_t n, int32_t sample_rate)>;

    bool configure(
        std::unique_ptr<TtsBackend> backend,
        const std::string& speaker_device,
        DoneCallback on_done,
        RenderTapCallback on_render = {});
    void start();
    void stop();
    void enqueue(const buddy_interfaces::msg::Sentence& msg);
    void clear_queue();
    void interrupt_now();
    void clear_interrupt();
    size_t pending_queue_size_for_test();

private:
    static constexpr size_t MAX_QUEUE_SIZE = 50;
    static constexpr int MAX_OPEN_RETRIES = 10;
    static constexpr auto IDLE_CLOSE_TIMEOUT = std::chrono::seconds(5);

    static std::string clean_tts_text(const std::string& text);
    void tts_loop();
    bool ensure_playback_open(int32_t sample_rate);
    void close_playback();
    bool play_speech(const float* samples, int32_t n, int32_t sample_rate);

    rclcpp::Logger logger_;
    std::unique_ptr<TtsBackend> backend_;
    std::string speaker_device_ = "default";
    DoneCallback on_done_;
    RenderTapCallback on_render_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> interrupt_requested_{false};
    std::mutex queue_mtx_;
    std::condition_variable queue_cv_;
    std::queue<buddy_interfaces::msg::Sentence> queue_;

    // Persistent playback handle
    snd_pcm_t* playback_ = nullptr;
    int32_t playback_rate_ = 0;
    std::chrono::steady_clock::time_point last_play_time_;
};
