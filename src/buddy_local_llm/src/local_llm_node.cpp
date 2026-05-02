#include "buddy_local_llm/local_llm_node.hpp"

#include <curl/curl.h>

LocalLlmNode::LocalLlmNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("local_llm", options) {}

CallbackReturn LocalLlmNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "LocalLlmNode: configuring");

  declare_parameter("model_name", "gemma4:e2b");
  declare_parameter("api_url", "http://localhost:11434");
  declare_parameter("timeout_seconds", 5);
  declare_parameter("system_prompt",
                    "你是一个友好的机器人助手，请用简短自然的方式回复");

  model_name_ = get_parameter("model_name").as_string();
  auto api_url = get_parameter("api_url").as_string();
  auto timeout = get_parameter("timeout_seconds").as_int();
  system_prompt_ = get_parameter("system_prompt").as_string();

  client_ = std::make_unique<OllamaClient>(api_url, model_name_, timeout);

  local_chunk_pub_ = create_publisher<buddy_interfaces::msg::InferenceChunk>(
      "/inference/local_chunk", 10);
  inference_request_sub_ =
      create_subscription<buddy_interfaces::msg::InferenceRequest>(
          "/brain/request", 10,
          std::bind(&LocalLlmNode::on_inference_request, this,
                    std::placeholders::_1));

  curl_global_init(CURL_GLOBAL_DEFAULT);
  return CallbackReturn::SUCCESS;
}

CallbackReturn LocalLlmNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "LocalLlmNode: activating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn LocalLlmNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "LocalLlmNode: deactivating");
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
  return CallbackReturn::SUCCESS;
}
CallbackReturn LocalLlmNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "LocalLlmNode: cleaning up");
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
  local_chunk_pub_.reset();
  inference_request_sub_.reset();
  client_.reset();
  curl_global_cleanup();
  return CallbackReturn::SUCCESS;
}
CallbackReturn LocalLlmNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "LocalLlmNode: shutting down");
  return CallbackReturn::SUCCESS;
}
CallbackReturn LocalLlmNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "LocalLlmNode: error");
  return CallbackReturn::SUCCESS;
}

void LocalLlmNode::on_inference_request(
    const buddy_interfaces::msg::InferenceRequest &msg) {
  RCLCPP_INFO(get_logger(), "Local request: %s", msg.user_text.c_str());
  std::lock_guard<std::mutex> lock(request_mtx_);
  if (worker_thread_.joinable()) {
    RCLCPP_WARN(get_logger(),
                "Previous local request still in progress, waiting");
    worker_thread_.join();
  }
  worker_thread_ = std::thread([this, msg]() { handle_request(msg); });
}

void LocalLlmNode::handle_request(
    const buddy_interfaces::msg::InferenceRequest &msg) {
  std::vector<ChatMessage> messages;
  if (!system_prompt_.empty()) {
    messages.push_back({"system", system_prompt_});
  }

  std::string user_text = msg.user_text;
  if (user_text.empty()) {
    user_text = "你好";
  }
  messages.push_back({"user", user_text});

  bool ok = client_->chat_streaming(
      messages, [this](const std::string &chunk, bool done) {
        auto msg = buddy_interfaces::msg::InferenceChunk();
        msg.session_id = "local";
        msg.chunk_text = chunk;
        msg.is_final = done;
        local_chunk_pub_->publish(msg);
      });

  if (!ok) {
    auto err = buddy_interfaces::msg::InferenceChunk();
    err.session_id = "local";
    err.chunk_text = "";
    err.is_final = true;
    local_chunk_pub_->publish(err);
    RCLCPP_WARN(get_logger(), "Local LLM request failed");
  }
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(LocalLlmNode)
