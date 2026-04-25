#include "buddy_vision/vision_pipeline_node.hpp"

VisionPipelineNode::VisionPipelineNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("vision", options) {}

CallbackReturn VisionPipelineNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: configuring");
  expression_pub_ = create_publisher<buddy_interfaces::msg::ExpressionResult>("/vision/expression", 10);
  capture_srv_ = create_service<buddy_interfaces::srv::CaptureImage>(
      "/vision/capture", std::bind(&VisionPipelineNode::handle_capture, this,
                std::placeholders::_1, std::placeholders::_2));
  return CallbackReturn::SUCCESS;
}
CallbackReturn VisionPipelineNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: activating"); return CallbackReturn::SUCCESS; }
CallbackReturn VisionPipelineNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: deactivating"); return CallbackReturn::SUCCESS; }
CallbackReturn VisionPipelineNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: cleaning up");
  expression_pub_.reset(); capture_srv_.reset(); return CallbackReturn::SUCCESS; }
CallbackReturn VisionPipelineNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: shutting down"); return CallbackReturn::SUCCESS; }
CallbackReturn VisionPipelineNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "VisionPipelineNode: error"); return CallbackReturn::SUCCESS; }

void VisionPipelineNode::handle_capture(
    const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request>,
    std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response>) {
  RCLCPP_INFO(get_logger(), "Capture requested, running expression recognition");
  // TODO: integrate real camera capture + RKNN model inference
  auto result = buddy_interfaces::msg::ExpressionResult();
  result.expression = "neutral";
  result.confidence = 0.95f;
  result.timestamp = now();
  expression_pub_->publish(result);
}

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(VisionPipelineNode)
