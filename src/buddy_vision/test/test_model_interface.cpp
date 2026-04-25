#include "buddy_vision/model_interface.hpp"
#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

TEST(MockModelTest, LoadReturnsTrue) {
  MockModel model;
  EXPECT_TRUE(model.load("/any/path"));
}

TEST(MockModelTest, InferenceReturnsFixedResult) {
  MockModel model;
  model.load("");
  cv::Mat frame = cv::Mat::zeros(224, 224, CV_8UC3);
  auto result = model.inference(frame);
  EXPECT_EQ(result.label, "neutral");
  EXPECT_FLOAT_EQ(result.confidence, 0.95f);
}

TEST(MockModelTest, UnloadDoesNotCrash) {
  MockModel model;
  model.load("");
  model.unload();
  SUCCEED();
}
