#include "buddy_vision/onnx_emotion_model.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <vector>

// ---------------------------------------------------------------------------
// load – create face detector (Haar cascade) + emotion classifier (ONNX RT)
// ---------------------------------------------------------------------------
bool EmotionOnnxModel::load(const std::string &model_dir) {
  // Face detection: OpenCV Haar cascade (built-in, no external model needed)
  std::string haar_path =
      "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml";
  if (!face_detector_.load(haar_path)) {
    std::fprintf(stderr,
                 "EmotionOnnxModel: failed to load Haar cascade from %s\n",
                 haar_path.c_str());
    return false;
  }

  // Emotion classification: ONNX Runtime
  std::string emotion_path = model_dir + "/emotion_classifier.onnx";
  session_opts_.SetIntraOpNumThreads(1);
  session_opts_.SetGraphOptimizationLevel(ORT_ENABLE_ALL);

  try {
    emotion_session_ = std::make_unique<Ort::Session>(
        env_, emotion_path.c_str(), session_opts_);
  } catch (const Ort::Exception &e) {
    std::fprintf(stderr, "EmotionOnnxModel: failed to load emotion model: %s\n",
                 e.what());
    return false;
  }

  loaded_ = true;
  return true;
}

// ---------------------------------------------------------------------------
// unload
// ---------------------------------------------------------------------------
void EmotionOnnxModel::unload() {
  emotion_session_.reset();
  loaded_ = false;
}

// ---------------------------------------------------------------------------
// inference – detect face -> crop -> classify emotion
// ---------------------------------------------------------------------------
ModelResult EmotionOnnxModel::inference(const cv::Mat &frame) {
  if (!loaded_) {
    return {"no_model", 0.0f};
  }

  auto face = detect_face(frame);
  if (face.empty()) {
    return {"no_face", 0.0f, {}};
  }

  float fx = face[0], fy = face[1], fw = face[2], fh = face[3];
  cv::Rect detected_rect(static_cast<int>(fx), static_cast<int>(fy),
                         static_cast<int>(fw), static_cast<int>(fh));

  float margin_x = fw * 0.2f;
  float margin_y = fh * 0.2f;
  int x1 = static_cast<int>(std::max(0.0f, fx - margin_x));
  int y1 = static_cast<int>(std::max(0.0f, fy - margin_y));
  int x2 = static_cast<int>(
      std::min(static_cast<float>(frame.cols), fx + fw + margin_x));
  int y2 = static_cast<int>(
      std::min(static_cast<float>(frame.rows), fy + fh + margin_y));

  cv::Rect crop_rect(x1, y1, x2 - x1, y2 - y1);
  cv::Mat face_crop = frame(crop_rect);

  auto probs = classify_emotion(face_crop);
  int idx = argmax(probs);
  return {kEmotionLabels[idx], probs[idx], detected_rect};
}

// ---------------------------------------------------------------------------
// detect_face – OpenCV Haar cascade
// ---------------------------------------------------------------------------
std::vector<float> EmotionOnnxModel::detect_face(const cv::Mat &bgr_frame) {
  cv::Mat gray;
  cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);

  std::vector<cv::Rect> faces;
  face_detector_.detectMultiScale(gray, faces, 1.1, 3, 0);

  if (faces.empty()) {
    return {};
  }

  // Return the largest face
  int best = 0;
  int best_area = 0;
  for (int i = 0; i < static_cast<int>(faces.size()); ++i) {
    int area = faces[i].width * faces[i].height;
    if (area > best_area) {
      best_area = area;
      best = i;
    }
  }

  return {static_cast<float>(faces[best].x), static_cast<float>(faces[best].y),
          static_cast<float>(faces[best].width),
          static_cast<float>(faces[best].height)};
}

// ---------------------------------------------------------------------------
// classify_emotion – ONNX Runtime inference + softmax
// ---------------------------------------------------------------------------
std::array<float, 7>
EmotionOnnxModel::classify_emotion(const cv::Mat &face_crop) {
  std::array<float, 7> probs{};

  Ort::AllocatorWithDefaultOptions allocator;
  auto input_tensor = mat_to_tensor(face_crop, 224, 224);

  auto input_name_alloc = emotion_session_->GetInputNameAllocated(0, allocator);
  const char *input_names[] = {input_name_alloc.get()};

  auto output_name_alloc =
      emotion_session_->GetOutputNameAllocated(0, allocator);
  const char *output_names[] = {output_name_alloc.get()};

  auto output_tensors = emotion_session_->Run(
      Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

  if (output_tensors.empty()) {
    return probs;
  }

  const float *logits = output_tensors[0].GetTensorData<float>();

  float max_val = *std::max_element(logits, logits + 7);
  float sum = 0.0f;
  for (int i = 0; i < 7; ++i) {
    probs[i] = std::exp(logits[i] - max_val);
    sum += probs[i];
  }
  for (auto &p : probs) {
    p /= sum;
  }

  return probs;
}

// ---------------------------------------------------------------------------
// mat_to_tensor – BGR cv::Mat -> Ort::Value [1, 3, H, W] float32 normalized
// ---------------------------------------------------------------------------
Ort::Value EmotionOnnxModel::mat_to_tensor(const cv::Mat &image, int target_w,
                                           int target_h) {
  cv::Mat resized;
  cv::resize(image, resized, cv::Size(target_w, target_h));

  cv::Mat rgb;
  cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
  rgb.convertTo(rgb, CV_32F, 1.0 / 255.0);

  size_t tensor_size = 3 * target_h * target_w;
  std::vector<float> tensor_data(tensor_size);
  for (int y = 0; y < target_h; ++y) {
    for (int x = 0; x < target_w; ++x) {
      cv::Vec3f pixel = rgb.at<cv::Vec3f>(y, x);
      tensor_data[0 * target_h * target_w + y * target_w + x] = pixel[0];
      tensor_data[1 * target_h * target_w + y * target_w + x] = pixel[1];
      tensor_data[2 * target_h * target_w + y * target_w + x] = pixel[2];
    }
  }

  std::array<int64_t, 4> shape = {1, 3, target_h, target_w};
  auto memory_info =
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  return Ort::Value::CreateTensor<float>(
      memory_info, tensor_data.data(), tensor_size, shape.data(), shape.size());
}

// ---------------------------------------------------------------------------
// argmax
// ---------------------------------------------------------------------------
int EmotionOnnxModel::argmax(const std::array<float, 7> &probs) {
  return static_cast<int>(std::distance(
      probs.begin(), std::max_element(probs.begin(), probs.end())));
}
