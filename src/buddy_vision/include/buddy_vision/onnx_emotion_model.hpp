#pragma once

#include "buddy_vision/model_interface.hpp"

#include <onnxruntime_cxx_api.h>
#include <opencv2/objdetect.hpp>
#include <array>
#include <memory>
#include <string>

class EmotionOnnxModel : public ModelInterface {
public:
  bool load(const std::string &model_dir) override;
  ModelResult inference(const cv::Mat &frame) override;
  void unload() override;

private:
  // Face detection via Haar cascade
  // Returns {x, y, w, h} of largest face, or empty if none
  std::vector<float> detect_face(const cv::Mat &bgr_frame);

  // Emotion classification via ONNX Runtime
  std::array<float, 7> classify_emotion(const cv::Mat &face_crop);

  // Convert cv::Mat to ONNX tensor [1, 3, H, W], float32, RGB, normalized 0-1
  Ort::Value mat_to_tensor(const cv::Mat &image, int target_w, int target_h);

  static int argmax(const std::array<float, 7> &probs);

  // Face detection (Haar cascade)
  cv::CascadeClassifier face_detector_;

  // Emotion classification (ONNX Runtime)
  Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "buddy-vision"};
  std::unique_ptr<Ort::Session> emotion_session_;
  Ort::SessionOptions session_opts_;
  bool loaded_{false};

  static constexpr const char *kEmotionLabels[] = {
      "angry", "disgust", "fear", "happy",
      "sad",   "surprise", "neutral"};
};
