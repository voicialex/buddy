#include "buddy_dialog/dialog_manager_node.hpp"

DialogManagerNode::DialogManagerNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("dialog", options) {}

CallbackReturn DialogManagerNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "DialogManagerNode: configuring");
  user_input_sub_ = create_subscription<buddy_interfaces::msg::UserInput>(
      "/dialog/user_input", 10,
      std::bind(&DialogManagerNode::on_user_input, this, std::placeholders::_1));
  return CallbackReturn::SUCCESS;
}
CallbackReturn DialogManagerNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "DialogManagerNode: activating"); return CallbackReturn::SUCCESS; }
CallbackReturn DialogManagerNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "DialogManagerNode: deactivating"); return CallbackReturn::SUCCESS; }
CallbackReturn DialogManagerNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "DialogManagerNode: cleaning up");
  user_input_sub_.reset(); return CallbackReturn::SUCCESS; }
CallbackReturn DialogManagerNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "DialogManagerNode: shutting down"); return CallbackReturn::SUCCESS; }
CallbackReturn DialogManagerNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "DialogManagerNode: error"); return CallbackReturn::SUCCESS; }

void DialogManagerNode::on_user_input(const buddy_interfaces::msg::UserInput &msg) {
  RCLCPP_INFO(get_logger(), "Dialog input [%s]: %s", msg.session_id.c_str(), msg.text.c_str());
}

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(DialogManagerNode)
