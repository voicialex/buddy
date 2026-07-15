#include "buddy_brain/asr_filter.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {
// 按 UTF-8 字符分割字符串（中文 3 字节/字符，不能按字节遍历）。
std::vector<std::string> split_utf8(const std::string& s) {
    std::vector<std::string> chars;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else { ++i; continue; }  // invalid leading byte
        if (i + len > s.size()) break;  // truncated
        chars.push_back(s.substr(i, len));
        i += len;
    }
    return chars;
}
}  // namespace

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
    // Fuzzy: UTF-8 字符 overlap >= 60%
    // 必须按 UTF-8 字符匹配，不能按字节（中文 3 字节/字符，按字节会误匹配）
    if (static_cast<int>(asr_norm.size()) >= min_chars_) {
        auto asr_chars = split_utf8(asr_norm);
        auto last_chars = split_utf8(last_norm);
        if (!asr_chars.empty()) {
            int overlap = 0;
            for (const auto& ch : asr_chars) {
                auto it = std::find(last_chars.begin(), last_chars.end(), ch);
                if (it != last_chars.end()) {
                    ++overlap;
                    last_chars.erase(it);  // mark used
                }
            }
            if (overlap * 100 / static_cast<int>(asr_chars.size()) >= 60) {
                if (reason != nullptr) {
                    std::ostringstream ss;
                    ss << "ASR echo filtered (fuzzy overlap=" << overlap
                       << "/" << asr_chars.size() << ")";
                    *reason = ss.str();
                }
                return true;
            }
        }
    }

    return false;
}

