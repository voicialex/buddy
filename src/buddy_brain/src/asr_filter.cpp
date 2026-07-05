#include "buddy_brain/asr_filter.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

EchoSubstringAsrFilter::EchoSubstringAsrFilter(double guard_seconds, int min_chars)
    : guard_seconds_(guard_seconds), min_chars_(min_chars) {}

std::string EchoSubstringAsrFilter::normalize_for_match(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (!std::isspace(c)) out.push_back(static_cast<char>(c));
    }
    return out;
}

bool EchoSubstringAsrFilter::should_filter(
    const std::string& asr_text, const AsrFilterContext& ctx, std::string* reason) const {
    if (!ctx.is_idle || !ctx.has_active_session || !ctx.has_tts_done_timestamp || ctx.last_assistant_response.empty()) {
        return false;
    }

    const double elapsed = ctx.elapsed_since_tts_done_sec;
    if (!(elapsed >= 0.0 && elapsed <= guard_seconds_)) {
        return false;
    }

    const std::string asr_norm = normalize_for_match(asr_text);
    const std::string last_norm = normalize_for_match(ctx.last_assistant_response);
    if (static_cast<int>(asr_norm.size()) < min_chars_) {
        return false;
    }
    // Exact substring match
    if (last_norm.find(asr_norm) != std::string::npos) {
        if (reason != nullptr) *reason = "ASR echo filtered (exact substring)";
        return true;
    }
    // Fuzzy: character overlap ratio >= 70%
    if (static_cast<int>(asr_norm.size()) >= min_chars_) {
        int overlap = 0;
        std::string tmp = last_norm;
        for (char c : asr_norm) {
            auto pos = tmp.find(c);
            if (pos != std::string::npos) {
                ++overlap;
                tmp[pos] = '\0';  // mark used
            }
        }
        if (overlap * 100 / static_cast<int>(asr_norm.size()) >= 60) {
            if (reason != nullptr) {
                std::ostringstream ss;
                ss << "ASR echo filtered (fuzzy overlap=" << overlap
                   << "/" << asr_norm.size() << ")";
                *reason = ss.str();
            }
            return true;
        }
    }

    return false;
}

