#pragma once

#include "buddy_vision/frame_buffer.hpp"
#include "buddy_vision/model_interface.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <opencv2/opencv.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <thread>

struct CameraConfig {
  std::string name;
  std::string device_path;
  int frame_width;
  int frame_height;
  std::string model_path;
  int model_input_width;
  int model_input_height;
  int inference_interval_ms;
};

class CameraWorker {
public:
  using ResultCallback =
      std::function<void(const std::string &label, float confidence)>;

  CameraWorker(const CameraConfig &config, rclcpp::Logger logger)
      : config_(config), logger_(logger),
        model_(std::make_unique<MockModel>()), running_(false) {}

  bool start() {
    cap_.open(config_.device_path, cv::CAP_V4L2);
    if (!cap_.isOpened()) {
      RCLCPP_WARN(logger_, "CameraWorker [%s]: failed to open %s",
                  config_.name.c_str(), config_.device_path.c_str());
      return false;
    }
    cap_.set(cv::CAP_PROP_FRAME_WIDTH, config_.frame_width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.frame_height);

    if (!model_->load(config_.model_path)) {
      RCLCPP_ERROR(logger_, "CameraWorker [%s]: failed to load model %s",
                   config_.name.c_str(), config_.model_path.c_str());
      model_loaded_ = false;
    } else {
      model_loaded_ = true;
    }

    running_ = true;
    capture_thread_ = std::thread(&CameraWorker::capture_loop, this);
    inference_thread_ = std::thread(&CameraWorker::inference_loop, this);
    RCLCPP_INFO(logger_, "CameraWorker [%s]: started", config_.name.c_str());
    return true;
  }

  void stop() {
    running_ = false;
    if (capture_thread_.joinable()) {
      capture_thread_.join();
    }
    if (inference_thread_.joinable()) {
      inference_thread_.join();
    }
    if (cap_.isOpened()) {
      cap_.release();
    }
    model_->unload();
    RCLCPP_INFO(logger_, "CameraWorker [%s]: stopped", config_.name.c_str());
  }

  bool get_latest_frame(cv::Mat &out) { return buffer_.snapshot(out); }

  void set_result_callback(ResultCallback cb) { result_cb_ = std::move(cb); }

private:
  void capture_loop() {
    cv::Mat frame;
    while (running_) {
      if (!cap_.isOpened()) {
        try_reconnect();
        continue;
      }
      if (!cap_.read(frame)) {
        RCLCPP_WARN(logger_, "CameraWorker [%s]: read failed, reconnecting",
                    config_.name.c_str());
        cap_.release();
        try_reconnect();
        continue;
      }
      buffer_.write(std::move(frame));
    }
  }

  void inference_loop() {
    while (running_) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(config_.inference_interval_ms));
      if (!running_ || !model_loaded_) {
        continue;
      }
      cv::Mat frame;
      if (!buffer_.snapshot(frame)) {
        continue;
      }
      cv::Mat processed;
      cv::resize(frame, processed,
                 cv::Size(config_.model_input_width, config_.model_input_height));
      cv::cvtColor(processed, processed, cv::COLOR_BGR2RGB);
      processed.convertTo(processed, CV_32F, 1.0 / 255.0);

      auto result = model_->inference(processed);
      if (result_cb_) {
        result_cb_(result.label, result.confidence);
      }
    }
  }

  void try_reconnect() {
    for (int attempt = 0; attempt < 3 && running_; ++attempt) {
      int delay_ms = 1000 * (attempt + 1);
      RCLCPP_INFO(logger_, "CameraWorker [%s]: reconnect attempt %d in %dms",
                  config_.name.c_str(), attempt + 1, delay_ms);
      auto deadline = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(delay_ms);
      while (running_ && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (!running_) return;
      cap_.open(config_.device_path, cv::CAP_V4L2);
      if (cap_.isOpened()) {
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, config_.frame_width);
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.frame_height);
        RCLCPP_INFO(logger_, "CameraWorker [%s]: reconnected",
                    config_.name.c_str());
        return;
      }
    }
    RCLCPP_ERROR(logger_,
                 "CameraWorker [%s]: failed to reconnect after 3 attempts",
                 config_.name.c_str());
  }

  CameraConfig config_;
  rclcpp::Logger logger_;
  FrameBuffer buffer_;
  std::unique_ptr<ModelInterface> model_;
  bool model_loaded_{false};
  std::atomic<bool> running_;
  std::thread capture_thread_;
  std::thread inference_thread_;
  cv::VideoCapture cap_;
  ResultCallback result_cb_;
};
