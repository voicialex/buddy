#include "buddy_dialog/dialog_manager_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class DialogNodeTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<DialogManagerNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<DialogManagerNode> node_;
};

TEST_F(DialogNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), std::string("dialog"));
}
TEST_F(DialogNodeTest, ConfigureTransition) {
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
}
TEST_F(DialogNodeTest, FullLifecycleSequence) {
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
