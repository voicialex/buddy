#include "buddy_cloud/cloud_client_node.hpp"

CloudClientNode::CloudClientNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("cloud", options) {}

CallbackReturn CloudClientNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: configuring");
  cloud_response_pub_ = create_publisher<buddy_interfaces::msg::CloudChunk>(
      "/cloud/response", 10);
  cloud_request_sub_ =
      create_subscription<buddy_interfaces::msg::CloudRequest>(
          "/brain/cloud_request", 10,
          std::bind(&CloudClientNode::on_cloud_request, this,
                    std::placeholders::_1));
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: activating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: deactivating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: cleaning up");
  cloud_response_pub_.reset();
  cloud_request_sub_.reset();
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: shutting down");
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "CloudClientNode: error");
  return CallbackReturn::SUCCESS;
}

void CloudClientNode::on_cloud_request(
    const buddy_interfaces::msg::CloudRequest &msg) {
  RCLCPP_INFO(get_logger(), "Cloud request [%s]: %s",
              msg.trigger_type.c_str(), msg.user_text.c_str());
  // TODO: replace with Doubao API call (Task 6)
  auto chunk = buddy_interfaces::msg::CloudChunk();
  chunk.session_id = "stub";
  chunk.chunk_text = "Hello, I am Buddy!";
  chunk.is_final = true;
  cloud_response_pub_->publish(chunk);
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(CloudClientNode)
