#pragma once

#include "buddy_vision/frame_buffer.hpp"
#include "buddy_vision/model_interface.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
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

  CameraWorker(const CameraConfig &config, rclcpp::Logger logger,
               std::unique_ptr<ModelInterface> model, bool preview = false)
      : config_(config), logger_(logger), model_(std::move(model)),
        running_(false), preview_enabled_(preview) {}

  ~CameraWorker() { stop(); }

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
    last_fps_time_ = std::chrono::steady_clock::now();
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
    std::string win_name = "Camera: " + config_.name;
    bool win_created = false;
    int fps_frame_count = 0;
    double current_fps = 0.0;

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

      // FPS calculation
      fps_frame_count++;
      auto now = std::chrono::steady_clock::now();
      auto fps_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - last_fps_time_)
                             .count();
      if (fps_elapsed >= 1000) {
        current_fps = fps_frame_count * 1000.0 / fps_elapsed;
        fps_frame_count = 0;
        last_fps_time_ = now;
      }

      if (preview_enabled_) {
        if (!win_created) {
          cv::namedWindow(win_name, cv::WINDOW_AUTOSIZE);
          win_created = true;
        }
        // Draw overlay from inference results
        cv::Mat display = frame.clone();
        {
          std::lock_guard<std::mutex> lock(overlay_mtx_);
          if (!overlay_face_rect_.empty()) {
            cv::rectangle(display, overlay_face_rect_, cv::Scalar(0, 255, 0),
                          2);
            std::string text =
                overlay_emotion_ + " " +
                std::to_string(static_cast<int>(overlay_confidence_ * 100)) +
                "%";
            int baseline = 0;
            auto text_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX,
                                             0.7, 2, &baseline);
            int text_y = overlay_face_rect_.y - 8;
            if (text_y < text_size.height) {
              text_y = overlay_face_rect_.y + text_size.height + 8;
            }
            cv::putText(display, text, cv::Point(overlay_face_rect_.x, text_y),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0),
                        2);
          }
        }
        // FPS top-left
        std::string fps_text =
            "FPS: " + std::to_string(static_cast<int>(current_fps));
        cv::putText(display, fps_text, cv::Point(10, 25),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);
        cv::imshow(win_name, display);
        cv::waitKey(1);
      }
      buffer_.write(std::move(frame));
    }
    if (win_created) {
      cv::destroyWindow(win_name);
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
      auto result = model_->inference(frame);

      // Update overlay for preview drawing
      {
        std::lock_guard<std::mutex> lock(overlay_mtx_);
        overlay_emotion_ = result.label;
        overlay_confidence_ = result.confidence;
        overlay_face_rect_ = result.face_rect;
      }

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
      if (!running_)
        return;
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
  bool preview_enabled_;

  // Overlay state (shared between capture & inference threads)
  std::mutex overlay_mtx_;
  std::string overlay_emotion_;
  float overlay_confidence_{0.f};
  cv::Rect overlay_face_rect_;

  // FPS tracking
  std::chrono::steady_clock::time_point last_fps_time_;
};
