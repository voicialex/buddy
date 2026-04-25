#include "buddy_vision/vision_pipeline_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class VisionNodeTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<VisionPipelineNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<VisionPipelineNode> node_;
};

TEST_F(VisionNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), std::string("vision"));
}

TEST_F(VisionNodeTest, ConfigureWithNoCameras) {
  // No YAML params loaded → discover_camera_names returns empty →
  // on_configure succeeds with zero workers
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
}

TEST_F(VisionNodeTest, FullLifecycleSequence) {
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
  EXPECT_STREQ(node_->activate().label().c_str(), "active");
  EXPECT_STREQ(node_->deactivate().label().c_str(), "inactive");
  EXPECT_STREQ(node_->cleanup().label().c_str(), "unconfigured");
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
