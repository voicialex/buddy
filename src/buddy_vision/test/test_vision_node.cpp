#include <gtest/gtest.h>

#include <rclcpp/rclcpp.hpp>

#include "buddy_vision/vision_pipeline_node.hpp"

class VisionNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        rclcpp::init(0, nullptr);
        node_ = std::make_shared<VisionPipelineNode>();
    }
    void TearDown() override {
        node_.reset();
        rclcpp::shutdown();
    }
    std::shared_ptr<VisionPipelineNode> node_;
};

TEST_F(VisionNodeTest, NodeName) {
    EXPECT_EQ(node_->get_name(), std::string("vision"));
}

TEST_F(VisionNodeTest, ConfigureWithNoCameras) {
    EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
}

TEST_F(VisionNodeTest, FullLifecycleSequence) {
    EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
    EXPECT_STREQ(node_->activate().label().c_str(), "active");
    EXPECT_STREQ(node_->deactivate().label().c_str(), "inactive");
    EXPECT_STREQ(node_->cleanup().label().c_str(), "unconfigured");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
