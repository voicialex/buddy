#pragma once

#include <string>

struct AsrFilterContext {
    bool is_idle{false};
    bool has_active_session{false};
    bool has_tts_done_timestamp{false};
    double elapsed_since_tts_done_sec{-1.0};
    std::string last_assistant_response;
};

class AsrFilter {
public:
    virtual ~AsrFilter() = default;
    virtual bool should_filter(const std::string& asr_text, const AsrFilterContext& ctx, std::string* reason) const = 0;
};

class EchoSubstringAsrFilter final : public AsrFilter {
public:
    EchoSubstringAsrFilter(double guard_seconds, int min_chars);
    bool should_filter(const std::string& asr_text, const AsrFilterContext& ctx, std::string* reason) const override;

private:
    static std::string normalize_for_match(const std::string& text);

    double guard_seconds_{8.0};
    int min_chars_{4};
};

