#pragma once
#include <buddy_interfaces/msg/sentence.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/empty.hpp>
#include <std_msgs/msg/string.hpp>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class AudioPipelineNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit AudioPipelineNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void on_sentence(const buddy_interfaces::msg::Sentence &msg);
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr wake_word_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr asr_text_pub_;
  rclcpp::Publisher<std_msgs::msg::Empty>::SharedPtr tts_done_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::Sentence>::SharedPtr
      sentence_sub_;
};
