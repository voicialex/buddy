#include "buddy_sentence/sentence_segmenter_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class SentenceNodeTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<SentenceSegmenterNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<SentenceSegmenterNode> node_;
};

TEST_F(SentenceNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), std::string("sentence"));
}

TEST_F(SentenceNodeTest, ConfigureTransition) {
  auto &state = node_->configure();
  EXPECT_STREQ(state.label().c_str(), "inactive");
}

TEST_F(SentenceNodeTest, FullLifecycleSequence) {
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
  EXPECT_STREQ(node_->activate().label().c_str(), "active");
  EXPECT_STREQ(node_->deactivate().label().c_str(), "inactive");
  EXPECT_STREQ(node_->cleanup().label().c_str(), "unconfigured");
}

TEST_F(SentenceNodeTest, SegmentChineseText) {
  auto segments = node_->segment("你好。我是Buddy！");
  EXPECT_GE(segments.size(), 2u);
  EXPECT_EQ(segments[0], "你好。");
  EXPECT_EQ(segments[1], "我是Buddy！");
}

TEST_F(SentenceNodeTest, SegmentNoEnding) {
  auto segments = node_->segment("no ending");
  EXPECT_EQ(segments.size(), 1u);
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
