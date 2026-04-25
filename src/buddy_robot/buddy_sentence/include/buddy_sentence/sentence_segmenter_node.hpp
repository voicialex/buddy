#pragma once
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/sentence.hpp>
#include <string>
#include <vector>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class SentenceSegmenterNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit SentenceSegmenterNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;
  // Public for testing
  std::vector<std::string> segment(const std::string &text);
private:
  void on_cloud_chunk(const buddy_interfaces::msg::CloudChunk &msg);
  void flush_buffer(const std::string &session_id);
  rclcpp::Publisher<buddy_interfaces::msg::Sentence>::SharedPtr sentence_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::CloudChunk>::SharedPtr cloud_sub_;
  std::string buffer_;
  uint32_t sentence_index_{0};
  std::string current_session_;
};
