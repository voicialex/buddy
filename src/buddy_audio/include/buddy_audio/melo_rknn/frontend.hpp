#pragma once

#include "tensor.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace melo_tts {

struct FrontendOptions {
    std::string resource_dir;
    std::string speaker;
    std::string bert_model_path;
    float speed = 1.0f;
    float sdp_ratio = 0.0f;
    float noise_scale = 0.6f;
    float noise_scale_w = 0.8f;
    std::string bert_mode = "zero";
    std::string rank = "rank1";
};

struct FrontendSegment {
    std::unordered_map<std::string, Tensor> tensors;
    std::string normalized_text;
};

class MeloFrontend {
public:
    explicit MeloFrontend(FrontendOptions options);

    std::vector<FrontendSegment> prepare(const std::string& text) const;
    int sample_rate() const { return sample_rate_; }

private:
    FrontendOptions options_;
    int sample_rate_ = 44100;
    int speaker_id_ = 1;
    std::unordered_map<std::string, int64_t> symbol_to_id_;
    std::unordered_map<std::string, int64_t> token_to_id_;
    std::unordered_map<uint32_t, std::string> codepoint_to_pinyin_;
    std::unordered_map<std::string, std::vector<std::string>> phrase_to_pinyin_;
    std::unordered_map<std::string, std::vector<std::string>> pinyin_to_symbols_;
    std::unordered_map<std::string, int64_t> jieba_freq_;
    std::unordered_map<std::string, std::string> jieba_pos_;
    std::unordered_set<std::string> must_neutral_tone_words_;
    std::unordered_set<std::string> must_not_neutral_tone_words_;
    int64_t jieba_total_ = 0;
};

}  // namespace melo_tts
