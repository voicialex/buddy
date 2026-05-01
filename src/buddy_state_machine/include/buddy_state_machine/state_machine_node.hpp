#pragma once
#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/display_command.hpp>
#include <buddy_interfaces/msg/emotion_result.hpp>
#include <buddy_interfaces/msg/user_input.hpp>
#include <buddy_interfaces/srv/capture_image.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/empty.hpp>
#include <std_msgs/msg/string.hpp>
#include <string>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class StateMachineNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit StateMachineNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  enum class State { IDLE, LISTENING, THINKING, SPEAKING };
  void on_wake_word(const std_msgs::msg::String &msg);
  void on_asr_text(const std_msgs::msg::String &msg);
  void on_emotion(const buddy_interfaces::msg::EmotionResult &msg);
  void on_cloud_response(const buddy_interfaces::msg::CloudChunk &msg);
  void on_tts_done(const std_msgs::msg::Empty &msg);
  void transition(State new_state);
  State state_{State::IDLE};
  std::string session_id_;
  rclcpp::Publisher<buddy_interfaces::msg::UserInput>::SharedPtr
      user_input_pub_;
  rclcpp::Publisher<buddy_interfaces::msg::DisplayCommand>::SharedPtr
      display_pub_;
  rclcpp::Client<buddy_interfaces::srv::CaptureImage>::SharedPtr
      capture_client_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr wake_word_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr asr_text_sub_;
  rclcpp::Subscription<buddy_interfaces::msg::EmotionResult>::SharedPtr
      emotion_sub_;
  rclcpp::Subscription<buddy_interfaces::msg::CloudChunk>::SharedPtr
      cloud_response_sub_;
  rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr tts_done_sub_;
};
