#pragma once

#include <string>
#include <opencv2/opencv.hpp>

struct ModelResult {
  std::string label;
  float confidence;
  cv::Rect face_rect;
};

class ModelInterface {
public:
  virtual ~ModelInterface() = default;
  virtual bool load(const std::string &model_path) = 0;
  virtual ModelResult inference(const cv::Mat &frame) = 0;
  virtual void unload() = 0;
};

class MockModel : public ModelInterface {
public:
  bool load(const std::string & /*model_path*/) override { return true; }
  ModelResult inference(const cv::Mat & /*frame*/) override {
    return {"neutral", 0.95f};
  }
  void unload() override {}
};
