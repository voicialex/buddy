#include "buddy_brain/image_capture_coordinator.hpp"

void ImageCaptureCoordinator::configure(
    rclcpp::Client<buddy_interfaces::srv::CaptureImage>::SharedPtr client, int timeout_ms) {
    client_ = std::move(client);
    timeout_ms_ = timeout_ms;
}

void ImageCaptureCoordinator::kick_off() {
    if (!enabled_ || !client_ || !client_->service_is_ready()) {
        return;
    }
    auto req = std::make_shared<buddy_interfaces::srv::CaptureImage::Request>();
    future_ = client_->async_send_request(req).share();
}

sensor_msgs::msg::Image ImageCaptureCoordinator::wait_and_get() {
    sensor_msgs::msg::Image result;
    if (!future_.valid()) return result;

    auto status = future_.wait_for(std::chrono::milliseconds(timeout_ms_));
    if (status == std::future_status::ready) {
        auto res = future_.get();
        if (res && !res->image.data.empty()) {
            result = res->image;
        }
    }
    future_ = {};
    return result;
}

void ImageCaptureCoordinator::reset_client() {
    client_.reset();
    future_ = {};
}
