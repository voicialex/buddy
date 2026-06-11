#include "buddy_brain/session_context.hpp"

#include <sstream>

std::string TurnMetrics::to_log_string() const {
    std::ostringstream ss;
    ss << "asr=";
    if (asr_latency_ms() >= 0) ss << static_cast<int>(asr_latency_ms()) << "ms";
    else ss << "n/a";
    ss << " llm_ft=";
    if (llm_first_token_ms() >= 0) ss << static_cast<int>(llm_first_token_ms()) << "ms";
    else ss << "n/a";
    ss << " 1st_sent=";
    if (first_sentence_ms() >= 0) ss << static_cast<int>(first_sentence_ms()) << "ms";
    else ss << "n/a";
    ss << " tts=";
    if (tts_total_ms() >= 0) ss << static_cast<int>(tts_total_ms()) << "ms";
    else ss << "n/a";
    ss << " e2e=";
    if (end_to_end_ms() >= 0) ss << static_cast<int>(end_to_end_ms()) << "ms";
    else ss << "n/a";
    return ss.str();
}

void SessionContext::configure(int max_history_turns, std::string system_prompt) {
    max_history_turns_ = max_history_turns;
    system_prompt_ = std::move(system_prompt);
}

void SessionContext::start_session() {
    auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    session_id_ = "sess-" + std::to_string(now_ms);
    turn_counter_ = 0;
    history_.clear();
}

std::string SessionContext::start_turn() {
    if (!is_active()) {
        start_session();
    }
    turn_id_ = session_id_ + "-t" + std::to_string(++turn_counter_);
    metrics_.reset();
    return turn_id_;
}

void SessionContext::add_user_text(const std::string& text) {
    if (!text.empty()) {
        history_.push_back("user: " + text);
        trim_history();
    }
}

void SessionContext::add_assistant_response(const std::string& text) {
    history_.push_back("assistant: " + text);
    trim_history();
}

void SessionContext::remove_last_user_turn() {
    if (!history_.empty() && history_.back().rfind("user: ", 0) == 0) {
        history_.pop_back();
    }
}

void SessionContext::clear() {
    session_id_.clear();
    turn_id_.clear();
    turn_counter_ = 0;
    history_.clear();
    metrics_.reset();
}

void SessionContext::trim_history() {
    while (static_cast<int>(history_.size()) > max_history_turns_ * 2) {
        history_.pop_front();
    }
}
