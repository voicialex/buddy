#include "buddy_state_machine/state_machine_node.hpp"
#include <chrono>

StateMachineNode::StateMachineNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("state_machine", options) {}

CallbackReturn StateMachineNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "StateMachineNode: configuring");
  user_input_pub_ = create_publisher<buddy_interfaces::msg::UserInput>(
      "/dialog/user_input", 10);
  display_pub_ = create_publisher<buddy_interfaces::msg::DisplayCommand>(
      "/display/command", 10);
  capture_client_ =
      create_client<buddy_interfaces::srv::CaptureImage>("/vision/capture");
  wake_word_sub_ = create_subscription<std_msgs::msg::String>(
      "/audio/wake_word", 10,
      std::bind(&StateMachineNode::on_wake_word, this, std::placeholders::_1));
  asr_text_sub_ = create_subscription<std_msgs::msg::String>(
      "/audio/asr_text", 10,
      std::bind(&StateMachineNode::on_asr_text, this, std::placeholders::_1));
  expression_sub_ =
      create_subscription<buddy_interfaces::msg::ExpressionResult>(
          "/vision/expression/result", 10,
          std::bind(&StateMachineNode::on_expression, this,
                    std::placeholders::_1));
  cloud_response_sub_ = create_subscription<buddy_interfaces::msg::CloudChunk>(
      "/dialog/cloud_response", 10,
      std::bind(&StateMachineNode::on_cloud_response, this,
                std::placeholders::_1));
  tts_done_sub_ = create_subscription<std_msgs::msg::Empty>(
      "/audio/tts_done", 10,
      std::bind(&StateMachineNode::on_tts_done, this, std::placeholders::_1));
  return CallbackReturn::SUCCESS;
}
CallbackReturn StateMachineNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "StateMachineNode: activating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn
StateMachineNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "StateMachineNode: deactivating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn StateMachineNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "StateMachineNode: cleaning up");
  user_input_pub_.reset();
  display_pub_.reset();
  capture_client_.reset();
  wake_word_sub_.reset();
  asr_text_sub_.reset();
  expression_sub_.reset();
  cloud_response_sub_.reset();
  tts_done_sub_.reset();
  return CallbackReturn::SUCCESS;
}
CallbackReturn StateMachineNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "StateMachineNode: shutting down");
  return CallbackReturn::SUCCESS;
}
CallbackReturn StateMachineNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "StateMachineNode: error");
  return CallbackReturn::SUCCESS;
}

void StateMachineNode::on_wake_word(const std_msgs::msg::String &msg) {
  RCLCPP_INFO(get_logger(), "[INTRA-DEMO] state_machine sub ptr: %p",
              (void *)&msg);
  RCLCPP_INFO(get_logger(), "Wake word detected");
  transition(State::LISTENING);
}

void StateMachineNode::on_asr_text(const std_msgs::msg::String &msg) {
  if (state_ != State::LISTENING)
    return;
  RCLCPP_INFO(get_logger(), "ASR: %s", msg.data.c_str());
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  session_id_ = "sess-" + std::to_string(now_ms);
  auto input = buddy_interfaces::msg::UserInput();
  input.text = msg.data;
  input.session_id = session_id_;
  input.timestamp = now();
  user_input_pub_->publish(input);
  transition(State::THINKING);
}

void StateMachineNode::on_expression(
    const buddy_interfaces::msg::ExpressionResult &msg) {
  RCLCPP_INFO(get_logger(), "Expression: %s (%.2f)", msg.expression.c_str(),
              msg.confidence);
}

void StateMachineNode::on_cloud_response(
    const buddy_interfaces::msg::CloudChunk &msg) {
  if (msg.is_final) {
    RCLCPP_INFO(get_logger(), "Cloud stream complete for session %s",
                msg.session_id.c_str());
  }
}

void StateMachineNode::on_tts_done(const std_msgs::msg::Empty &) {
  if (state_ == State::SPEAKING) {
    RCLCPP_INFO(get_logger(), "TTS done, returning to idle");
    transition(State::IDLE);
  }
}

void StateMachineNode::transition(State new_state) {
  static const char *names[] = {"IDLE", "LISTENING", "THINKING", "SPEAKING"};
  RCLCPP_INFO(get_logger(), "State: %s -> %s", names[static_cast<int>(state_)],
              names[static_cast<int>(new_state)]);
  state_ = new_state;
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(StateMachineNode)
