#pragma once
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <buddy_interfaces/msg/expression_result.hpp>
#include <buddy_interfaces/srv/capture_image.hpp>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class VisionPipelineNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit VisionPipelineNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;
private:
  void handle_capture(
      const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request> request,
      std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> response);
  rclcpp::Publisher<buddy_interfaces::msg::ExpressionResult>::SharedPtr expression_pub_;
  rclcpp::Service<buddy_interfaces::srv::CaptureImage>::SharedPtr capture_srv_;
};
