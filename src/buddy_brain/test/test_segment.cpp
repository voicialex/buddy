#include "buddy_brain/brain_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class SegmentTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<BrainNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<BrainNode> node_;
};

TEST_F(SegmentTest, SplitEnglishSentences) {
  auto result = node_->segment("Hello world. How are you? Fine!");
  EXPECT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0], "Hello world.");
  EXPECT_EQ(result[1], " How are you?");
  EXPECT_EQ(result[2], " Fine!");
}

TEST_F(SegmentTest, SplitChineseSentences) {
  auto result = node_->segment("你好世界。你好吗？很好！");
  EXPECT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0], "你好世界。");
  EXPECT_EQ(result[1], "你好吗？");
  EXPECT_EQ(result[2], "很好！");
}

TEST_F(SegmentTest, BuffersIncompleteText) {
  auto result = node_->segment("Hello world");
  EXPECT_EQ(result.size(), 0u);
  auto result2 = node_->segment(". More text.");
  EXPECT_EQ(result2.size(), 2u);
  EXPECT_EQ(result2[0], "Hello world.");
  EXPECT_EQ(result2[1], " More text.");
}

TEST_F(SegmentTest, EmptyInput) {
  auto result = node_->segment("");
  EXPECT_EQ(result.size(), 0u);
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
