#include "buddy_cloud/cloud_client_node.hpp"

CloudClientNode::CloudClientNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("cloud", options) {}

CallbackReturn CloudClientNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: configuring");
  cloud_response_pub_ = create_publisher<buddy_interfaces::msg::CloudChunk>("/dialog/cloud_response", 10);
  user_input_sub_ = create_subscription<buddy_interfaces::msg::UserInput>(
      "/dialog/user_input", 10,
      std::bind(&CloudClientNode::on_user_input, this, std::placeholders::_1));
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: activating"); return CallbackReturn::SUCCESS; }
CallbackReturn CloudClientNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: deactivating"); return CallbackReturn::SUCCESS; }
CallbackReturn CloudClientNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: cleaning up");
  cloud_response_pub_.reset(); user_input_sub_.reset(); return CallbackReturn::SUCCESS; }
CallbackReturn CloudClientNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: shutting down"); return CallbackReturn::SUCCESS; }
CallbackReturn CloudClientNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "CloudClientNode: error"); return CallbackReturn::SUCCESS; }

void CloudClientNode::on_user_input(const buddy_interfaces::msg::UserInput &msg) {
  RCLCPP_INFO(get_logger(), "Cloud request for session %s: %s", msg.session_id.c_str(), msg.text.c_str());
  // TODO: integrate real WebSocket connection to gateway
  auto chunk = buddy_interfaces::msg::CloudChunk();
  chunk.session_id = msg.session_id;
  chunk.chunk_text = "Hello, I am Buddy!";
  chunk.is_final = true;
  cloud_response_pub_->publish(chunk);
}

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(CloudClientNode)
