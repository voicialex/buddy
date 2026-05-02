#pragma once

#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/cloud_request.hpp>
#include <buddy_interfaces/msg/emotion_result.hpp>
#include <buddy_interfaces/msg/sentence.hpp>
#include <buddy_interfaces/srv/capture_image.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/empty.hpp>
#include <std_msgs/msg/string.hpp>

#include <chrono>
#include <deque>
#include <string>
#include <vector>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class BrainNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit BrainNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

  // Public for testing
  enum class State { IDLE, LISTENING, EMOTION_TRIGGER, REQUESTING, SPEAKING };
  State state() const { return state_; }
  std::vector<std::string> segment(const std::string &text);

private:
  void on_wake_word(const std_msgs::msg::String &msg);
  void on_asr_text(const std_msgs::msg::String &msg);
  void on_emotion(const buddy_interfaces::msg::EmotionResult &msg);
  void on_cloud_chunk(const buddy_interfaces::msg::CloudChunk &msg);
  void on_tts_done(const std_msgs::msg::Empty &msg);

  void transition(State new_state);
  void request_cloud(const std::string &trigger_type,
                     const std::string &user_text);
  void flush_sentence_buffer(const std::string &session_id);
  void trim_history();

  State state_{State::IDLE};
  std::string session_id_;

  std::deque<std::string> history_;
  int max_history_turns_{10};
  std::string system_prompt_;

  std::string current_emotion_;
  float emotion_confidence_{0.f};
  std::chrono::steady_clock::time_point negative_since_;
  std::chrono::steady_clock::time_point last_proactive_trigger_;
  bool tracking_negative_{false};

  bool emotion_trigger_enabled_{true};
  std::vector<std::string> negative_emotions_{"sad", "angry", "fear"};
  float emotion_confidence_threshold_{0.7f};
  double emotion_duration_seconds_{3.0};
  double emotion_cooldown_seconds_{60.0};
  bool voice_attach_image_{true};

  std::string sentence_buffer_;
  uint32_t sentence_index_{0};

  rclcpp::Publisher<buddy_interfaces::msg::CloudRequest>::SharedPtr
      cloud_request_pub_;
  rclcpp::Publisher<buddy_interfaces::msg::Sentence>::SharedPtr sentence_pub_;

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr wake_word_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr asr_text_sub_;
  rclcpp::Subscription<buddy_interfaces::msg::EmotionResult>::SharedPtr
      emotion_sub_;
  rclcpp::Subscription<buddy_interfaces::msg::CloudChunk>::SharedPtr
      cloud_chunk_sub_;
  rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr tts_done_sub_;

  rclcpp::Client<buddy_interfaces::srv::CaptureImage>::SharedPtr
      capture_client_;
};
