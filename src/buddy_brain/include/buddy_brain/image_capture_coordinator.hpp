#pragma once

#include <buddy_interfaces/srv/capture_image.hpp>
#include <future>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

class ImageCaptureCoordinator {
public:
    void configure(rclcpp::Client<buddy_interfaces::srv::CaptureImage>::SharedPtr client, int timeout_ms);

    void kick_off();

    sensor_msgs::msg::Image wait_and_get();

    bool is_enabled() const { return enabled_; }
    void set_enabled(bool v) { enabled_ = v; }

    void reset_client();

private:
    rclcpp::Client<buddy_interfaces::srv::CaptureImage>::SharedPtr client_;
    std::shared_future<buddy_interfaces::srv::CaptureImage::Response::SharedPtr> future_;
    int timeout_ms_{1200};
    bool enabled_{true};
};
