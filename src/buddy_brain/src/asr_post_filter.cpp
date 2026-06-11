#include "buddy_brain/asr_post_filter.hpp"

void AsrPostFilter::configure(std::unique_ptr<AsrFilter> filter) {
    filter_ = std::move(filter);
}

bool AsrPostFilter::should_filter(const std::string& asr_text, bool is_idle, bool has_active_session,
                                  const std::string& last_assistant_response,
                                  std::chrono::steady_clock::time_point last_tts_done_at,
                                  bool has_tts_done_timestamp, std::string* reason) const {
    if (!filter_) return false;

    const double elapsed = has_tts_done_timestamp
        ? std::chrono::duration<double>(std::chrono::steady_clock::now() - last_tts_done_at).count()
        : -1.0;

    AsrFilterContext ctx{};
    ctx.is_idle = is_idle;
    ctx.has_active_session = has_active_session;
    ctx.has_tts_done_timestamp = has_tts_done_timestamp;
    ctx.elapsed_since_tts_done_sec = elapsed;
    ctx.last_assistant_response = last_assistant_response;

    return filter_->should_filter(asr_text, ctx, reason);
}
