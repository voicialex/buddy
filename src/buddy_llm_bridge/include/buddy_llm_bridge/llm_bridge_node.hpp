#pragma once

#include "inference_server_base.hpp"

class LlmBridgeNode : public InferenceServerBase {
   public:
    explicit LlmBridgeNode(const rclcpp::NodeOptions& options);

    CallbackReturn on_configure(const rclcpp_lifecycle::State&) override;

   protected:
    void execute(std::shared_ptr<GoalHandle> goal_handle) override;

   private:
    std::string server_url_;
    std::string mode_;
    int request_timeout_sec_ = 120;
};
