#include "buddy_audio/audio_pipeline_node.hpp"

AudioPipelineNode::AudioPipelineNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("audio", options) {}

CallbackReturn
AudioPipelineNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: configuring");
  wake_word_pub_ =
      create_publisher<std_msgs::msg::String>("/audio/wake_word", 10);
  asr_text_pub_ =
      create_publisher<std_msgs::msg::String>("/audio/asr_text", 10);
  tts_done_pub_ = create_publisher<std_msgs::msg::Empty>("/audio/tts_done", 10);
  sentence_sub_ = create_subscription<buddy_interfaces::msg::Sentence>(
      "/dialog/sentence", 10,
      std::bind(&AudioPipelineNode::on_sentence, this, std::placeholders::_1));
  return CallbackReturn::SUCCESS;
}
CallbackReturn AudioPipelineNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: activating");
  // INTRA-DEMO: publish with unique_ptr for zero-copy verification
  auto msg = std::make_unique<std_msgs::msg::String>();
  msg->data = "hey_buddy";
  RCLCPP_INFO(get_logger(), "[INTRA-DEMO] audio pub ptr: %p",
              (void *)msg.get());
  wake_word_pub_->publish(std::move(msg));
  return CallbackReturn::SUCCESS;
}
CallbackReturn
AudioPipelineNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: deactivating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn AudioPipelineNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: cleaning up");
  wake_word_pub_.reset();
  asr_text_pub_.reset();
  tts_done_pub_.reset();
  sentence_sub_.reset();
  return CallbackReturn::SUCCESS;
}
CallbackReturn AudioPipelineNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: shutting down");
  return CallbackReturn::SUCCESS;
}
CallbackReturn AudioPipelineNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "AudioPipelineNode: error");
  return CallbackReturn::SUCCESS;
}

void AudioPipelineNode::on_sentence(
    const buddy_interfaces::msg::Sentence &msg) {
  RCLCPP_INFO(get_logger(), "TTS: playing sentence [%u]: %s", msg.index,
              msg.text.c_str());
  std_msgs::msg::Empty done;
  tts_done_pub_->publish(done);
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(AudioPipelineNode)
