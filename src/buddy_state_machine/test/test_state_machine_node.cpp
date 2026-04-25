#include "buddy_state_machine/state_machine_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class StateMachineNodeTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<StateMachineNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<StateMachineNode> node_;
};

TEST_F(StateMachineNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), std::string("state_machine"));
}
TEST_F(StateMachineNodeTest, ConfigureTransition) {
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
}
TEST_F(StateMachineNodeTest, FullLifecycleSequence) {
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
