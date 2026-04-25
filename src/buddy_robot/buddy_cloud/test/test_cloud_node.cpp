#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include "buddy_cloud/cloud_client_node.hpp"

class CloudNodeTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<CloudClientNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<CloudClientNode> node_;
};

TEST_F(CloudNodeTest, NodeName) { EXPECT_EQ(node_->get_name(), std::string("cloud")); }
TEST_F(CloudNodeTest, ConfigureTransition) { EXPECT_STREQ(node_->configure().label().c_str(), "inactive"); }
TEST_F(CloudNodeTest, FullLifecycleSequence) {
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
