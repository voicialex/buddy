#pragma once

#include "buddy_vision/model_interface.hpp"

#include <array>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <opencv2/objdetect.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <vector>

class EmotionOnnxModel : public ModelInterface {
public:
  explicit EmotionOnnxModel(rclcpp::Logger logger) : logger_(logger) {}
  bool load(const std::string &model_dir) override;
  ModelResult inference(const cv::Mat &frame) override;
  void unload() override;

private:
  std::vector<float> detect_face(const cv::Mat &bgr_frame);
  std::array<float, 7> classify_emotion(const cv::Mat &face_crop);
  Ort::Value mat_to_tensor(const cv::Mat &image, int target_w, int target_h);
  static int argmax(const std::array<float, 7> &probs);

  rclcpp::Logger logger_;
  cv::CascadeClassifier face_detector_;
  Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "buddy-vision"};
  std::unique_ptr<Ort::Session> emotion_session_;
  Ort::SessionOptions session_opts_;
  bool loaded_{false};

  // Pre-allocated tensor buffer to avoid per-inference allocation
  std::vector<float> tensor_data_;

  static constexpr const char *kEmotionLabels[] = {
      "angry", "disgust", "fear", "happy", "sad", "surprise", "neutral"};
};
