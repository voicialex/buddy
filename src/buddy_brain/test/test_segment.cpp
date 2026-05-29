#include <gtest/gtest.h>

#include "buddy_brain/sentence_segmenter.hpp"

class SegmentTest : public ::testing::Test {
protected:
    SentenceSegmenter seg_;
};

TEST_F(SegmentTest, SplitEnglishSentences) {
    auto result = seg_.feed("Hello world. How are you? Fine!");
    EXPECT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "Hello world.");
    EXPECT_EQ(result[1], " How are you?");
    EXPECT_EQ(result[2], " Fine!");
}

TEST_F(SegmentTest, SplitChineseSentences) {
    auto result = seg_.feed("你好世界。你好吗？很好！");
    EXPECT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "你好世界。");
    EXPECT_EQ(result[1], "你好吗？");
    EXPECT_EQ(result[2], "很好！");
}

TEST_F(SegmentTest, BuffersIncompleteText) {
    auto result = seg_.feed("Hello world");
    EXPECT_EQ(result.size(), 0u);
    auto result2 = seg_.feed(". More text.");
    EXPECT_EQ(result2.size(), 2u);
    EXPECT_EQ(result2[0], "Hello world.");
    EXPECT_EQ(result2[1], " More text.");
}

TEST_F(SegmentTest, EmptyInput) {
    auto result = seg_.feed("");
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(SegmentTest, FlushRemainder) {
    seg_.feed("Hello world");
    auto remainder = seg_.flush();
    EXPECT_EQ(remainder, "Hello world");
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
