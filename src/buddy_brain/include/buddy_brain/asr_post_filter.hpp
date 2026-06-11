#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "buddy_brain/asr_filter.hpp"

/// Wraps an AsrFilter strategy and encapsulates context construction.
/// Callers pass the raw state; the filter builds AsrFilterContext internally.
class AsrPostFilter {
public:
    void configure(std::unique_ptr<AsrFilter> filter);

    bool should_filter(const std::string& asr_text, bool is_idle, bool has_active_session,
                       const std::string& last_assistant_response,
                       std::chrono::steady_clock::time_point last_tts_done_at, bool has_tts_done_timestamp,
                       std::string* reason) const;

    bool has_filter() const { return static_cast<bool>(filter_); }

private:
    std::unique_ptr<AsrFilter> filter_;
};
