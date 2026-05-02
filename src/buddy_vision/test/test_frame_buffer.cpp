#include "buddy_vision/frame_buffer.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

TEST(FrameBufferTest, WriteThenSnapshotReturnsLatest) {
  FrameBuffer buf;
  cv::Mat frame1 = cv::Mat::zeros(480, 640, CV_8UC3);
  frame1.at<cv::Vec3b>(0, 0) = {1, 2, 3};
  buf.write(frame1.clone());

  cv::Mat out;
  ASSERT_TRUE(buf.snapshot(out));
  EXPECT_EQ(out.at<cv::Vec3b>(0, 0), cv::Vec3b(1, 2, 3));
}

TEST(FrameBufferTest, SnapshotBeforeWriteReturnsFalse) {
  FrameBuffer buf;
  cv::Mat out;
  EXPECT_FALSE(buf.snapshot(out));
}

TEST(FrameBufferTest, OverwriteKeepsLatest) {
  FrameBuffer buf;
  cv::Mat frame1 = cv::Mat::zeros(480, 640, CV_8UC3);
  frame1.at<cv::Vec3b>(0, 0) = {10, 20, 30};
  cv::Mat frame2 = cv::Mat::zeros(480, 640, CV_8UC3);
  frame2.at<cv::Vec3b>(0, 0) = {40, 50, 60};

  buf.write(frame1.clone());
  buf.write(frame2.clone());

  cv::Mat out;
  ASSERT_TRUE(buf.snapshot(out));
  EXPECT_EQ(out.at<cv::Vec3b>(0, 0), cv::Vec3b(40, 50, 60));
}

TEST(FrameBufferTest, ConcurrentWriteAndSnapshot) {
  FrameBuffer buf;
  std::atomic<bool> done{false};
  int write_count = 0;

  std::thread writer([&] {
    for (int i = 1; i <= 100; ++i) {
      cv::Mat frame = cv::Mat::zeros(480, 640, CV_8UC3);
      frame.at<cv::Vec3b>(0, 0) = {static_cast<uchar>(i % 256), 0, 0};
      buf.write(std::move(frame));
      write_count = i;
    }
    done = true;
  });

  std::thread reader([&] {
    while (!done) {
      cv::Mat out;
      buf.snapshot(out);
    }
  });

  writer.join();
  reader.join();
  EXPECT_EQ(write_count, 100);
}
