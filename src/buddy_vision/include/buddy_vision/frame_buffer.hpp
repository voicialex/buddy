#pragma once

#include <mutex>
#include <opencv2/opencv.hpp>

class FrameBuffer {
public:
  void write(cv::Mat frame) {
    std::lock_guard<std::mutex> lock(mtx_);
    buffers_[back_] = std::move(frame);
    back_ = 1 - back_;
    has_frame_ = true;
  }

  bool snapshot(cv::Mat &out) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!has_frame_) {
      return false;
    }
    out = buffers_[1 - back_].clone();
    return true;
  }

private:
  cv::Mat buffers_[2];
  int back_{0};
  bool has_frame_{false};
  std::mutex mtx_;
};
