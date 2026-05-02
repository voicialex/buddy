#pragma once
#include <buddy_interfaces/msg/inference_chunk.hpp>
#include <buddy_interfaces/msg/inference_request.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

#include "buddy_local_llm/ollama_client.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <thread>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class LocalLlmNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit LocalLlmNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void on_inference_request(const buddy_interfaces::msg::InferenceRequest &msg);
  void handle_request(const buddy_interfaces::msg::InferenceRequest &msg);

  rclcpp::Publisher<buddy_interfaces::msg::InferenceChunk>::SharedPtr
      local_chunk_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::InferenceRequest>::SharedPtr
      inference_request_sub_;

  std::unique_ptr<OllamaClient> client_;
  std::string model_name_;
  std::string system_prompt_;

  std::thread worker_thread_;
  std::mutex request_mtx_;
};
