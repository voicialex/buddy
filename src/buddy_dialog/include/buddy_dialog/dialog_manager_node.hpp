#pragma once
#include <buddy_interfaces/msg/user_input.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class DialogManagerNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit DialogManagerNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void on_user_input(const buddy_interfaces::msg::UserInput &msg);
  rclcpp::Subscription<buddy_interfaces::msg::UserInput>::SharedPtr
      user_input_sub_;
};
