#pragma once

#include "buddy_vision/camera_worker.hpp"

#include <buddy_interfaces/msg/expression_result.hpp>
#include <buddy_interfaces/srv/capture_image.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class VisionPipelineNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit VisionPipelineNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  std::vector<std::string> discover_camera_names();
  CameraConfig load_camera_config(const std::string &name);
  void handle_capture(
      const std::string &camera_name,
      const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request>
          request,
      std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> response);

  std::map<std::string, std::unique_ptr<CameraWorker>> workers_;
  std::map<std::string,
           rclcpp::Publisher<buddy_interfaces::msg::ExpressionResult>::SharedPtr>
      result_pubs_;
  std::map<std::string,
           rclcpp::Service<buddy_interfaces::srv::CaptureImage>::SharedPtr>
      capture_srvs_;
};
