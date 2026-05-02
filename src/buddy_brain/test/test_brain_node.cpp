#include "buddy_brain/brain_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class BrainNodeTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<BrainNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<BrainNode> node_;
};

TEST_F(BrainNodeTest, NodeName) {
  EXPECT_EQ(std::string(node_->get_name()), "brain");
}

TEST_F(BrainNodeTest, InitialStateIsIdle) {
  EXPECT_EQ(node_->state(), BrainNode::State::IDLE);
}

TEST_F(BrainNodeTest, ConfigureTransition) {
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
}

TEST_F(BrainNodeTest, FullLifecycleSequence) {
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
  EXPECT_STREQ(node_->activate().label().c_str(), "active");
  EXPECT_STREQ(node_->deactivate().label().c_str(), "inactive");
  EXPECT_STREQ(node_->cleanup().label().c_str(), "unconfigured");
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
