#include <gtest/gtest.h>

#include <rclcpp/rclcpp.hpp>

#include "buddy_audio/tts_backend.hpp"
#include "buddy_audio/tts_player.hpp"

namespace {

class DummyBackend final : public TtsBackend {
public:
    bool initialize(const TtsBackendConfig&, rclcpp::Logger) override { return true; }

    TtsResult generate(const std::string&) override { return {}; }
};

}  // namespace

TEST(TtsPlayerInterruptTest, InterruptClearsQueuedSentences) {
    TtsPlayer player(rclcpp::get_logger("tts_player_interrupt_test"));
    ASSERT_TRUE(player.configure(std::make_unique<DummyBackend>(), "default", []() {}));

    buddy_interfaces::msg::Sentence sentence_1;
    sentence_1.text = "hello";
    buddy_interfaces::msg::Sentence sentence_2;
    sentence_2.text = "world";
    player.enqueue(sentence_1);
    player.enqueue(sentence_2);

    EXPECT_EQ(player.pending_queue_size_for_test(), 2u);
    player.interrupt_now();
    EXPECT_EQ(player.pending_queue_size_for_test(), 0u);
}

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    rclcpp::shutdown();
    return result;
}
