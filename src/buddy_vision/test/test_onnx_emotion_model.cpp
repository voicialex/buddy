#include "buddy_vision/onnx_emotion_model.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

TEST(EmotionOnnxModelTest, LoadFailsWithEmptyPath) {
  EmotionOnnxModel model;
  EXPECT_FALSE(model.load(""));
}

TEST(EmotionOnnxModelTest, LoadFailsWithNonexistentPath) {
  EmotionOnnxModel model;
  EXPECT_FALSE(model.load("/nonexistent/path/to/models"));
}

TEST(EmotionOnnxModelTest, InferenceWithoutLoadReturnsNoModel) {
  EmotionOnnxModel model;
  cv::Mat frame = cv::Mat::zeros(480, 640, CV_8UC3);
  auto result = model.inference(frame);
  EXPECT_EQ(result.label, "no_model");
  EXPECT_FLOAT_EQ(result.confidence, 0.0f);
}

TEST(EmotionOnnxModelTest, UnloadWithoutLoadDoesNotCrash) {
  EmotionOnnxModel model;
  model.unload();
  SUCCEED();
}
