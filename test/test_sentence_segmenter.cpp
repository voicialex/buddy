#include <gtest/gtest.h>
#include "brain_proc/sentence_segmenter.h"
#include <vector>
#include <string>

// 辅助：将 tokens 逐个喂入分割器，收集所有句子
static std::vector<std::string> Feed(
    const std::vector<std::string>& tokens,
    bool flush = true)
{
    std::vector<std::string> sentences;
    SentenceSegmenter seg([&](const std::string& s) {
        sentences.push_back(s);
    });
    for (const auto& t : tokens) seg.Feed(t);
    if (flush) seg.Flush();
    return sentences;
}

// ---- 核心行为：首句逗号切 ----
TEST(SentenceSegmenter, FirstSentenceCommaChineseCut) {
    auto result = Feed({"今天", "天气", "不错", "，", "适合", "出门"}, false);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "今天天气不错，");
}

TEST(SentenceSegmenter, FirstSentenceCommAsciiCut) {
    auto result = Feed({"hello", ",", " world"}, false);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "hello,");
}

// ---- 后续句子等句号 ----
TEST(SentenceSegmenter, SubsequentSentenceWaitsPeriod) {
    // 喂入第一句（逗号切），然后第二句的逗号不应再切
    auto result = Feed({"你好", "，", "今天", "天气", "不错", "，", "真好", "。"});
    ASSERT_GE(result.size(), 2u);
    EXPECT_EQ(result[0], "你好，");          // 第一句：逗号切
    EXPECT_EQ(result[1], "今天天气不错，真好。"); // 第二句：逗号不切，等句号
}

// ---- 多种标点 ----
TEST(SentenceSegmenter, ChineseQuestionMark) {
    auto result = Feed({"你", "好吗", "？"});
    ASSERT_GE(result.size(), 1u);
    EXPECT_TRUE(result[0].find("？") != std::string::npos);
}

TEST(SentenceSegmenter, ExclamationMark) {
    auto result = Feed({"哇", "！"});
    ASSERT_GE(result.size(), 1u);
}

// ---- Flush 收尾 ----
TEST(SentenceSegmenter, FlushRemainder) {
    auto result = Feed({"未完", "待续"}, true /* flush */);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "未完待续");
}

TEST(SentenceSegmenter, FlushEmptyBuffer) {
    // 没有剩余 buffer 时 flush 不应产生空字符串
    auto result = Feed({"你好", "。"}, true);
    for (const auto& s : result) {
        EXPECT_FALSE(s.empty());
    }
}

// ---- Reset 后重新计数 ----
TEST(SentenceSegmenter, ResetRestoresFirstSentenceBehavior) {
    std::vector<std::string> sentences;
    SentenceSegmenter seg([&](const std::string& s) { sentences.push_back(s); });

    // 第一轮
    seg.Feed("你好"); seg.Feed("，"); seg.Flush(); seg.Reset();
    size_t after_first_round = sentences.size();

    // 第二轮：Reset 后，逗号应该又能切第一句
    seg.Feed("再见"); seg.Feed("，");
    EXPECT_GT(sentences.size(), after_first_round);
}

// ---- 边界情况 ----
TEST(SentenceSegmenter, EmptyToken) {
    // 空 token 不应崩溃
    auto result = Feed({"", "你好", "。"});
    EXPECT_FALSE(result.empty());
}

TEST(SentenceSegmenter, SingleToken) {
    auto result = Feed({"。"});
    // 单个句号也应该被处理
    EXPECT_EQ(result.size(), 1u);
}
