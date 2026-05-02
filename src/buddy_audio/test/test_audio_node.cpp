#include "buddy_audio/audio_pipeline_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class AudioNodeTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<AudioPipelineNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<AudioPipelineNode> node_;
};

TEST_F(AudioNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), std::string("audio"));
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
