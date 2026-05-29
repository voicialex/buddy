#pragma once

#include <atomic>
#include <buddy_interfaces/action/inference.hpp>
#include <functional>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <string>
#include <thread>

/// Base class for inference action servers (local_llm, cloud).
/// Provides lifecycle boilerplate, action server setup, and worker thread
/// management. Subclasses only implement on_configure() and execute().
class InferenceServerBase : public rclcpp_lifecycle::LifecycleNode {
public:
    using Inference = buddy_interfaces::action::Inference;
    using GoalHandle = rclcpp_action::ServerGoalHandle<Inference>;
    using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

    explicit InferenceServerBase(const std::string& name,
                                 const std::string& action_topic,
                                 const rclcpp::NodeOptions& options)
        : rclcpp_lifecycle::LifecycleNode(name, options), action_topic_(action_topic) {}

    ~InferenceServerBase() override {
        cancel_requested_.store(true);
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

protected:
    /// Subclass must implement: perform the actual inference work.
    virtual void execute(std::shared_ptr<GoalHandle> goal_handle) = 0;

    /// Call this at the end of subclass on_configure() to set up the action
    /// server.
    void create_action_server() {
        using namespace std::placeholders;
        action_server_ =
            rclcpp_action::create_server<Inference>(this,
                                                    action_topic_,
                                                    std::bind(&InferenceServerBase::handle_goal, this, _1, _2),
                                                    std::bind(&InferenceServerBase::handle_cancel, this, _1),
                                                    std::bind(&InferenceServerBase::handle_accepted, this, _1));
    }

    /// Subclass checks this in execute() to support cancellation.
    std::atomic<bool> cancel_requested_{false};

    // Default lifecycle implementations — subclass can override if needed.
    CallbackReturn on_activate(const rclcpp_lifecycle::State&) override {
        RCLCPP_INFO(get_logger(), "%s: activating", get_name());
        return CallbackReturn::SUCCESS;
    }

    CallbackReturn on_deactivate(const rclcpp_lifecycle::State&) override {
        RCLCPP_INFO(get_logger(), "%s: deactivating", get_name());
        join_worker();
        return CallbackReturn::SUCCESS;
    }

    CallbackReturn on_cleanup(const rclcpp_lifecycle::State&) override {
        RCLCPP_INFO(get_logger(), "%s: cleaning up", get_name());
        join_worker();
        action_server_.reset();
        return CallbackReturn::SUCCESS;
    }

    CallbackReturn on_shutdown(const rclcpp_lifecycle::State&) override {
        RCLCPP_INFO(get_logger(), "%s: shutting down", get_name());
        join_worker();
        return CallbackReturn::SUCCESS;
    }

    CallbackReturn on_error(const rclcpp_lifecycle::State&) override {
        RCLCPP_ERROR(get_logger(), "%s: error", get_name());
        return CallbackReturn::SUCCESS;
    }

private:
    void join_worker() {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    rclcpp_action::GoalResponse handle_goal(const rclcpp_action::GoalUUID& /*uuid*/,
                                            std::shared_ptr<const Inference::Goal> /*goal*/) {
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
    }

    rclcpp_action::CancelResponse handle_cancel(const std::shared_ptr<GoalHandle> /*goal_handle*/) {
        RCLCPP_INFO(get_logger(), "%s: cancel requested", get_name());
        cancel_requested_.store(true);
        return rclcpp_action::CancelResponse::ACCEPT;
    }

    void handle_accepted(const std::shared_ptr<GoalHandle> goal_handle) {
        std::lock_guard<std::mutex> lock(mtx_);
        join_worker();
        cancel_requested_.store(false);
        worker_thread_ = std::thread([this, goal_handle]() { execute(goal_handle); });
    }

    std::string action_topic_;
    rclcpp_action::Server<Inference>::SharedPtr action_server_;
    std::thread worker_thread_;
    std::mutex mtx_;
};
