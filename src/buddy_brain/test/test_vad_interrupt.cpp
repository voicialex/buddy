#include <gtest/gtest.h>

#include <rclcpp/rclcpp.hpp>

#include "buddy_brain/brain_node.hpp"

class BrainVadInterruptTest : public ::testing::Test {
protected:
    void SetUp() override {
        rclcpp::NodeOptions opts;
        opts.parameter_overrides({
            {"vad_interrupt.enabled", true},
            {"vad_interrupt.voice_frame_threshold", 5},
        });
        node_ = std::make_shared<BrainNode>(opts);
        node_->configure();
        node_->activate();
    }
    void TearDown() override { node_.reset(); }
    std::shared_ptr<BrainNode> node_;
};

TEST_F(BrainVadInterruptTest, NoInterruptBelowThreshold) {
    node_->test_set_state(BrainNode::State::SPEAKING);
    for (int i = 0; i < 4; ++i) node_->test_inject_voice_activity(true);
    EXPECT_EQ(node_->state(), BrainNode::State::SPEAKING);
    EXPECT_EQ(node_->test_vad_voice_frame_count(), 4);
}

TEST_F(BrainVadInterruptTest, InterruptsAtThreshold) {
    node_->test_set_state(BrainNode::State::SPEAKING);
    for (int i = 0; i < 5; ++i) node_->test_inject_voice_activity(true);
    EXPECT_EQ(node_->state(), BrainNode::State::LISTENING);
    EXPECT_EQ(node_->test_vad_voice_frame_count(), 0);
}

TEST_F(BrainVadInterruptTest, FalseResetsCounter) {
    node_->test_set_state(BrainNode::State::SPEAKING);
    for (int i = 0; i < 3; ++i) node_->test_inject_voice_activity(true);
    node_->test_inject_voice_activity(false);
    for (int i = 0; i < 4; ++i) node_->test_inject_voice_activity(true);
    EXPECT_EQ(node_->state(), BrainNode::State::SPEAKING);
}

TEST_F(BrainVadInterruptTest, NoInterruptInIdle) {
    node_->test_set_state(BrainNode::State::IDLE);
    for (int i = 0; i < 10; ++i) node_->test_inject_voice_activity(true);
    EXPECT_EQ(node_->state(), BrainNode::State::IDLE);
}

TEST_F(BrainVadInterruptTest, InterruptsInRequesting) {
    node_->test_set_state(BrainNode::State::REQUESTING);
    for (int i = 0; i < 5; ++i) node_->test_inject_voice_activity(true);
    EXPECT_EQ(node_->state(), BrainNode::State::LISTENING);
}

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    auto r = RUN_ALL_TESTS();
    rclcpp::shutdown();
    return r;
}
