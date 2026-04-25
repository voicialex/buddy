#include "buddy_sentence/sentence_segmenter_node.hpp"

SentenceSegmenterNode::SentenceSegmenterNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("sentence", options) {}

CallbackReturn SentenceSegmenterNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "SentenceSegmenterNode: configuring");
  sentence_pub_ = create_publisher<buddy_interfaces::msg::Sentence>("/dialog/sentence", 10);
  cloud_sub_ = create_subscription<buddy_interfaces::msg::CloudChunk>(
      "/dialog/cloud_response", 10,
      std::bind(&SentenceSegmenterNode::on_cloud_chunk, this, std::placeholders::_1));
  return CallbackReturn::SUCCESS;
}
CallbackReturn SentenceSegmenterNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "SentenceSegmenterNode: activating"); return CallbackReturn::SUCCESS; }
CallbackReturn SentenceSegmenterNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "SentenceSegmenterNode: deactivating"); return CallbackReturn::SUCCESS; }
CallbackReturn SentenceSegmenterNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "SentenceSegmenterNode: cleaning up");
  sentence_pub_.reset(); cloud_sub_.reset();
  buffer_.clear(); sentence_index_ = 0; return CallbackReturn::SUCCESS; }
CallbackReturn SentenceSegmenterNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "SentenceSegmenterNode: shutting down"); return CallbackReturn::SUCCESS; }
CallbackReturn SentenceSegmenterNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "SentenceSegmenterNode: error"); return CallbackReturn::SUCCESS; }

void SentenceSegmenterNode::on_cloud_chunk(const buddy_interfaces::msg::CloudChunk &msg) {
  if (current_session_.empty()) {
    current_session_ = msg.session_id;
    sentence_index_ = 0;
  }
  buffer_ += msg.chunk_text;
  auto sentences = segment(buffer_);
  if (sentences.size() > 1) {
    for (size_t i = 0; i < sentences.size() - 1; ++i) {
      auto s = buddy_interfaces::msg::Sentence();
      s.session_id = current_session_;
      s.text = sentences[i];
      s.index = sentence_index_++;
      sentence_pub_->publish(s);
    }
    buffer_ = sentences.back();
  }
  if (msg.is_final && !buffer_.empty()) {
    flush_buffer(current_session_);
    current_session_.clear();
  }
}

void SentenceSegmenterNode::flush_buffer(const std::string &session_id) {
  if (buffer_.empty()) return;
  auto s = buddy_interfaces::msg::Sentence();
  s.session_id = session_id;
  s.text = buffer_;
  s.index = sentence_index_++;
  sentence_pub_->publish(s);
  buffer_.clear();
}

std::vector<std::string> SentenceSegmenterNode::segment(const std::string &text) {
  std::vector<std::string> result;
  size_t last = 0;
  // UTF-8 Chinese punctuation: 。=3bytes, ！=3bytes, ？=3bytes
  // Plus ASCII: . ! ?
  const std::vector<std::string> delimiters = {
    "\xe3\x80\x82",  // 。
    "\xef\xbc\x81",  // ！
    "\xef\xbc\x9f",  // ？
    ".", "!", "?"
  };
  while (last < text.size()) {
    size_t best = std::string::npos;
    for (const auto &d : delimiters) {
      auto found = text.find(d, last);
      if (found != std::string::npos && (best == std::string::npos || found < best)) {
        best = found + d.size();
      }
    }
    if (best == std::string::npos) break;
    result.push_back(text.substr(last, best - last));
    last = best;
  }
  if (last < text.size()) {
    result.push_back(text.substr(last));
  }
  if (result.empty()) {
    result.push_back(text);
  }
  return result;
}

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(SentenceSegmenterNode)
