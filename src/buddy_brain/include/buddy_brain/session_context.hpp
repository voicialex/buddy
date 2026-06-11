#pragma once

#include <chrono>
#include <deque>
#include <string>

struct TurnMetrics {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint wake_at{};
    TimePoint asr_at{};
    TimePoint llm_first_token_at{};
    TimePoint llm_result_at{};
    TimePoint first_sentence_at{};
    TimePoint tts_done_at{};

    double asr_latency_ms() const {
        if (wake_at == TimePoint{} || asr_at == TimePoint{}) return -1.0;
        return std::chrono::duration<double, std::milli>(asr_at - wake_at).count();
    }

    double llm_first_token_ms() const {
        if (asr_at == TimePoint{} || llm_first_token_at == TimePoint{}) return -1.0;
        return std::chrono::duration<double, std::milli>(llm_first_token_at - asr_at).count();
    }

    double first_sentence_ms() const {
        if (asr_at == TimePoint{} || first_sentence_at == TimePoint{}) return -1.0;
        return std::chrono::duration<double, std::milli>(first_sentence_at - asr_at).count();
    }

    double tts_total_ms() const {
        if (first_sentence_at == TimePoint{} || tts_done_at == TimePoint{}) return -1.0;
        return std::chrono::duration<double, std::milli>(tts_done_at - first_sentence_at).count();
    }

    double end_to_end_ms() const {
        if (wake_at == TimePoint{} || tts_done_at == TimePoint{}) return -1.0;
        return std::chrono::duration<double, std::milli>(tts_done_at - wake_at).count();
    }

    void reset() {
        wake_at = {};
        asr_at = {};
        llm_first_token_at = {};
        llm_result_at = {};
        first_sentence_at = {};
        tts_done_at = {};
    }

    std::string to_log_string() const;
};

class SessionContext {
public:
    void configure(int max_history_turns, std::string system_prompt);

    bool is_active() const { return !session_id_.empty(); }
    const std::string& session_id() const { return session_id_; }
    const std::string& turn_id() const { return turn_id_; }
    const std::string& system_prompt() const { return system_prompt_; }

    void start_session();
    std::string start_turn();
    void add_user_text(const std::string& text);
    void add_assistant_response(const std::string& text);
    void remove_last_user_turn();
    void clear();

    const std::deque<std::string>& history() const { return history_; }

    TurnMetrics& metrics() { return metrics_; }
    const TurnMetrics& metrics() const { return metrics_; }

private:
    std::string session_id_;
    std::string turn_id_;
    int turn_counter_{0};
    int max_history_turns_{10};
    std::string system_prompt_;
    std::deque<std::string> history_;
    TurnMetrics metrics_;

    void trim_history();
};
