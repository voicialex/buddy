#pragma once
#include <atomic>
#include <buddy_interfaces/msg/sentence.hpp>
#include <buddy_interfaces/msg/tts_control.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/empty.hpp>
#include <std_msgs/msg/string.hpp>
#include <string>
#include <thread>

#include "buddy_audio/io/mic_capture.hpp"
#include "buddy_audio/io/audio_preprocessor.hpp"
#include "buddy_audio/io/speaker_player.hpp"
#include "buddy_audio/speech_recognizer.hpp"

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class AudioPipelineNode : public rclcpp_lifecycle::LifecycleNode {
public:
    explicit AudioPipelineNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
    ~AudioPipelineNode() override;
    CallbackReturn on_configure(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_activate(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_deactivate(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_cleanup(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_shutdown(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_error(const rclcpp_lifecycle::State&) override;

private:
    void capture_loop();
    void on_sentence(const buddy_interfaces::msg::Sentence& msg);
    void on_tts_control(const buddy_interfaces::msg::TtsControl& msg);

    std::unique_ptr<AudioCapture> capture_;
    std::unique_ptr<AudioPreprocessor> preprocessor_;
    std::unique_ptr<SpeechRecognizer> recognizer_;
    std::unique_ptr<TtsPlayer> tts_player_;

    // 麦克风采集线程：阻塞 ALSA read，绕过 ROS executor。
    // 边界：仅 publish 消息（线程安全），不等待 service response。
    // 风险：若未来需在 capture_loop 内调用 service，会与 executor 死锁。
    std::thread capture_thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> cooldown_chunks_remaining_{0};
    std::atomic<int> wake_guard_chunks_remaining_{0};
    int asr_cooldown_chunks_{5};    // 500ms at 100ms/chunk
    int asr_wake_guard_chunks_{7};  // 700ms at 100ms/chunk

    // ROS interfaces
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr wake_word_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr asr_text_pub_;
    rclcpp::Publisher<std_msgs::msg::Empty>::SharedPtr tts_done_pub_;
    rclcpp::Subscription<buddy_interfaces::msg::Sentence>::SharedPtr sentence_sub_;
    rclcpp::Subscription<buddy_interfaces::msg::TtsControl>::SharedPtr tts_control_sub_;

    int sample_rate_ = 16000;
    bool kws_enabled_ = true;
    std::string current_turn_id_;
};
