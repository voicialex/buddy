#pragma once

#include <buddy_interfaces/msg/emotion_result.hpp>
#include <buddy_interfaces/srv/capture_image.hpp>
#if __has_include(<cv_bridge/cv_bridge.hpp>)
#include <cv_bridge/cv_bridge.hpp>
#else
#include <cv_bridge/cv_bridge.h>
#endif
#include <map>
#include <memory>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <string>
#include <unordered_map>

#include "buddy_vision/model_interface.hpp"

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class VisionPipelineNode : public rclcpp_lifecycle::LifecycleNode {
public:
    explicit VisionPipelineNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

    CallbackReturn on_configure(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_activate(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_deactivate(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_cleanup(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_shutdown(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_error(const rclcpp_lifecycle::State&) override;

private:
    void on_emotion_frame(const sensor_msgs::msg::Image::ConstSharedPtr& msg);
    void on_game_frame(const sensor_msgs::msg::Image::ConstSharedPtr& msg);
    void update_camera_state(const std::string& name);
    void check_camera_timeouts();
    void do_inference();
    void handle_capture(const std::string& camera_name,
                        const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request>,
                        std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> response);

    // Subscriptions (raw sensor_msgs/Image, compatible with LifecycleNode)
    rclcpp::CallbackGroup::SharedPtr sub_callback_group_;
    rclcpp::CallbackGroup::SharedPtr timer_callback_group_;
    rclcpp::CallbackGroup::SharedPtr service_callback_group_;

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr emotion_sub_;
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr game_sub_;

    // Publishers
    rclcpp::Publisher<buddy_interfaces::msg::EmotionResult>::SharedPtr emotion_pub_;

    // Services
    std::map<std::string, rclcpp::Service<buddy_interfaces::srv::CaptureImage>::SharedPtr> capture_srvs_;

    // Inference
    rclcpp::TimerBase::SharedPtr inference_timer_;
    std::unique_ptr<ModelInterface> emotion_model_;

    // Frame cache
    sensor_msgs::msg::Image::ConstSharedPtr latest_emotion_frame_;
    sensor_msgs::msg::Image::ConstSharedPtr latest_game_frame_;

    // Camera online tracking
    struct CameraState {
        bool online{false};
        rclcpp::Time last_frame_time{0, 0, RCL_ROS_TIME};
    };
    std::unordered_map<std::string, CameraState> camera_states_;

    // Deduplication state
    std::string last_label_;
    float last_confidence_{0.0f};

    // Protect frame cache and camera states shared across callback groups
    std::mutex frame_mutex_;
    std::mutex camera_state_mutex_;
};
