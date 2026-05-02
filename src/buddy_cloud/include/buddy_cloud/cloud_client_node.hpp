#pragma once
#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/cloud_request.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

#include <mutex>
#include <string>
#include <thread>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class CloudClientNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit CloudClientNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void on_cloud_request(const buddy_interfaces::msg::CloudRequest &msg);
  void call_doubao(const buddy_interfaces::msg::CloudRequest &msg);
  std::string encode_image_base64(const sensor_msgs::msg::Image &image,
                                  int max_width);

  rclcpp::Publisher<buddy_interfaces::msg::CloudChunk>::SharedPtr
      cloud_response_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::CloudRequest>::SharedPtr
      cloud_request_sub_;

  std::string api_key_;
  std::string model_;
  std::string endpoint_;
  int image_max_width_{512};
  int timeout_seconds_{30};

  std::thread worker_thread_;
  std::mutex request_mtx_;
};
