#include "buddy_brain/brain_node.hpp"

#include <algorithm>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
struct WakePhraseLoadResult {
    std::vector<std::string> phrases;
    std::string source_path;
};

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

WakePhraseLoadResult load_wake_phrases_from_keywords_file() {
    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back("src/buddy_app/params/keywords.txt");
    candidates.emplace_back("output/install/buddy_app/share/buddy_app/params/keywords.txt");

    const char* ament_prefix = std::getenv("AMENT_PREFIX_PATH");
    if (ament_prefix) {
        std::istringstream ss(ament_prefix);
        std::string token;
        while (std::getline(ss, token, ':')) {
            const std::filesystem::path p(token);
            if (p.filename() == "buddy_app") {
                candidates.push_back(p / "share" / "buddy_app" / "params" / "keywords.txt");
                break;
            }
        }
    }

    WakePhraseLoadResult result;
    for (const auto& path : candidates) {
        if (!std::filesystem::exists(path)) continue;
        std::ifstream f(path);
        if (!f.is_open()) continue;
        std::string line;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            const auto at = line.find('@');
            const std::string display = at == std::string::npos ? line : trim(line.substr(at + 1));
            if (!display.empty()) result.phrases.push_back(display);
        }
        if (!result.phrases.empty()) {
            result.source_path = path.string();
            break;
        }
    }
    return result;
}

std::unique_ptr<AsrPostFilter> make_asr_post_filter(const std::string& strategy, double guard_seconds, int min_chars) {
    std::unique_ptr<AsrFilter> filter;
    if (strategy == "echo_substring") {
        filter = std::make_unique<EchoSubstringAsrFilter>(guard_seconds, min_chars);
    } else if (strategy != "none") {
        return {};
    }
    auto post_filter = std::make_unique<AsrPostFilter>();
    post_filter->configure(std::move(filter));
    return post_filter;
}
}  // namespace

BrainNode::BrainNode(const rclcpp::NodeOptions& options) : rclcpp_lifecycle::LifecycleNode("brain", options) {}

CallbackReturn BrainNode::on_configure(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "BrainNode: configuring");

    declare_parameter("system_prompt_path", "");
    declare_parameter("max_history_turns", 10);
    declare_parameter("emotion_trigger.enabled", true);
    declare_parameter("emotion_trigger.negative_emotions", std::vector<std::string>{"sad", "angry", "fear"});
    declare_parameter("emotion_trigger.confidence_threshold", 0.7);
    declare_parameter("emotion_trigger.duration_seconds", 3.0);
    declare_parameter("emotion_trigger.cooldown_seconds", 60.0);
    declare_parameter("voice_trigger.attach_image", true);
    declare_parameter("voice_trigger.capture_timeout_ms", 1200);
    declare_parameter("voice_trigger.followup_echo_guard_seconds", 8.0);
    declare_parameter("voice_trigger.followup_echo_guard_min_chars", 4);
    declare_parameter("voice_trigger.asr_filter.strategy", "echo_substring");
    declare_parameter("session_timeout_seconds", 60.0);
    declare_parameter("speaking_watchdog_seconds", 60.0);
    declare_parameter("fallback_sentence", "抱歉，我现在无法回答");
    declare_parameter("wake_phrase_fallbacks", std::vector<std::string>{"嘿巴迪"});

    const int max_history_turns = get_parameter("max_history_turns").as_int();
    session_timeout_seconds_ = get_parameter("session_timeout_seconds").as_double();
    watchdog_timeout_ms_ = static_cast<int>(get_parameter("speaking_watchdog_seconds").as_double() * 1000);

    // ASR filter
    const double followup_echo_guard_seconds = get_parameter("voice_trigger.followup_echo_guard_seconds").as_double();
    const int followup_echo_guard_min_chars = get_parameter("voice_trigger.followup_echo_guard_min_chars").as_int();
    const std::string asr_filter_strategy = get_parameter("voice_trigger.asr_filter.strategy").as_string();
    asr_filter_ = make_asr_post_filter(asr_filter_strategy, followup_echo_guard_seconds, followup_echo_guard_min_chars);
    if (asr_filter_strategy != "none" && !asr_filter_->has_filter()) {
        RCLCPP_ERROR(get_logger(),
                     "Invalid voice_trigger.asr_filter.strategy='%s'. Allowed: echo_substring | none",
                     asr_filter_strategy.c_str());
        return CallbackReturn::ERROR;
    }
    RCLCPP_INFO(get_logger(), "ASR filter strategy: %s", asr_filter_strategy.c_str());

    // Wake phrases
    const WakePhraseLoadResult wake_phrase_load = load_wake_phrases_from_keywords_file();
    wake_phrase_fallbacks_ = wake_phrase_load.phrases;
    if (wake_phrase_fallbacks_.empty()) {
        wake_phrase_fallbacks_ = get_parameter("wake_phrase_fallbacks").as_string_array();
        RCLCPP_WARN(get_logger(), "No keywords.txt found, using configured fallbacks (%zu phrases)",
                    wake_phrase_fallbacks_.size());
    }
    fallback_sentence_ = get_parameter("fallback_sentence").as_string();

    // Emotion trigger (nullptr when disabled)
    const bool emotion_trigger_enabled = get_parameter("emotion_trigger.enabled").as_bool();
    if (emotion_trigger_enabled) {
        EmotionTrigger::Config emo_cfg;
        emo_cfg.negative_emotions = get_parameter("emotion_trigger.negative_emotions").as_string_array();
        emo_cfg.confidence_threshold =
            static_cast<float>(get_parameter("emotion_trigger.confidence_threshold").as_double());
        emo_cfg.duration_seconds = get_parameter("emotion_trigger.duration_seconds").as_double();
        emo_cfg.cooldown_seconds = get_parameter("emotion_trigger.cooldown_seconds").as_double();
        emotion_trigger_ = std::make_unique<EmotionTrigger>(emo_cfg);
    }

    // System prompt
    std::string system_prompt;
    auto prompt_path = get_parameter("system_prompt_path").as_string();
    if (!prompt_path.empty()) {
        std::filesystem::path p(prompt_path);
        if (p.is_relative()) {
            try {
                auto share_dir = ament_index_cpp::get_package_share_directory("buddy_app");
                p = std::filesystem::path(share_dir) / prompt_path;
            } catch (const std::exception& e) {
                RCLCPP_WARN(get_logger(), "Failed to resolve package share dir: %s, using raw path", e.what());
                p = prompt_path;
            }
        }
        std::ifstream f(p);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            system_prompt = ss.str();
        }
    }
    session_.configure(max_history_turns, std::move(system_prompt));

    // Image capture coordinator
    const bool voice_attach_image = get_parameter("voice_trigger.attach_image").as_bool();
    int voice_capture_timeout_ms = get_parameter("voice_trigger.capture_timeout_ms").as_int();
    if (voice_capture_timeout_ms < 0) voice_capture_timeout_ms = 0;
    image_capture_.set_enabled(voice_attach_image);

    // LLM action client
    llm_client_ = rclcpp_action::create_client<Inference>(this, "/inference/llm");

    // Publishers
    sentence_pub_ = create_publisher<buddy_interfaces::msg::Sentence>("/brain/sentence", 10);
    response_pub_ = create_publisher<std_msgs::msg::String>("/brain/response", 10);
    tts_control_pub_ = create_publisher<buddy_interfaces::msg::TtsControl>("/brain/tts_control", 10);

    // Subscribers
    wake_word_sub_ = create_subscription<std_msgs::msg::String>(
        "/audio/wake_word", 10, std::bind(&BrainNode::on_wake_word, this, std::placeholders::_1));
    asr_text_sub_ = create_subscription<std_msgs::msg::String>(
        "/audio/asr_text", 10, std::bind(&BrainNode::on_asr_text, this, std::placeholders::_1));
    emotion_sub_ = create_subscription<buddy_interfaces::msg::EmotionResult>(
        "/vision/emotion/result", 10, std::bind(&BrainNode::on_emotion, this, std::placeholders::_1));
    tts_done_sub_ = create_subscription<std_msgs::msg::Empty>(
        "/audio/tts_done", 10, std::bind(&BrainNode::on_tts_done, this, std::placeholders::_1));

    // Capture client on separate callback group to avoid blocking service responses
    capture_client_group_ = create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    auto capture_client = create_client<buddy_interfaces::srv::CaptureImage>(
        "/vision/game/capture",
        rmw_qos_profile_services_default,
        capture_client_group_);
    image_capture_.configure(capture_client, voice_capture_timeout_ms);

    return CallbackReturn::SUCCESS;
}

CallbackReturn BrainNode::on_activate(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "BrainNode: activating");
    return CallbackReturn::SUCCESS;
}

CallbackReturn BrainNode::on_deactivate(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "BrainNode: deactivating");
    return CallbackReturn::SUCCESS;
}

CallbackReturn BrainNode::on_cleanup(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "BrainNode: cleaning up");
    llm_client_.reset();
    sentence_pub_.reset();
    response_pub_.reset();
    tts_control_pub_.reset();
    wake_word_sub_.reset();
    asr_text_sub_.reset();
    emotion_sub_.reset();
    tts_done_sub_.reset();
    image_capture_.reset_client();
    capture_client_group_.reset();
    session_.clear();
    segmenter_.reset();
    sentence_index_ = 0;
    return CallbackReturn::SUCCESS;
}

CallbackReturn BrainNode::on_shutdown(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "BrainNode: shutting down");
    return CallbackReturn::SUCCESS;
}

CallbackReturn BrainNode::on_error(const rclcpp_lifecycle::State&) {
    RCLCPP_ERROR(get_logger(), "BrainNode: error");
    return CallbackReturn::SUCCESS;
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void BrainNode::on_wake_word(const std_msgs::msg::String& msg) {
    if (msg.data == "__no_kws__") {
        kws_enabled_ = false;
    }
    if (state_ != State::IDLE) {
        RCLCPP_INFO(get_logger(), "Wake word: barge-in, cancelling current turn");
        cancel_current_turn();
    }
    RCLCPP_INFO(get_logger(), "Wake word detected");
    session_.metrics().wake_at = std::chrono::steady_clock::now();
    reset_session_timer();
    transition(State::LISTENING);
}

void BrainNode::on_asr_text(const std_msgs::msg::String& msg) {
    const bool has_active_session = session_.is_active();
    if (state_ == State::IDLE && !has_active_session) {
        if (!kws_enabled_) {
            reset_session_timer();
            transition(State::LISTENING);
        } else {
            const std::string asr_text = trim(msg.data);
            bool wake_matched = false;
            for (const auto& phrase : wake_phrase_fallbacks_) {
                if (!phrase.empty() && (asr_text == phrase || asr_text.rfind(phrase, 0) == 0)) {
                    wake_matched = true;
                    break;
                }
            }
            if (!wake_matched) {
                return;
            }
            RCLCPP_INFO(get_logger(), "ASR wake fallback matched");
            reset_session_timer();
            transition(State::LISTENING);
            return;
        }
    }

    if (asr_filter_->has_filter()) {
        std::string reason;
        if (asr_filter_->should_filter(msg.data, state_ == State::IDLE, has_active_session,
                                       last_assistant_response_, last_tts_done_at_,
                                       has_tts_done_timestamp_, &reason)) {
            if (reason.empty()) reason = "ASR filtered by active noise strategy";
            RCLCPP_INFO(get_logger(), "%s", reason.c_str());
            return;
        }
    }

    if (state_ == State::IDLE) {
        if (session_.metrics().wake_at == TurnMetrics::TimePoint{}) {
            session_.metrics().wake_at = std::chrono::steady_clock::now();
        }
        transition(State::LISTENING);
    } else if (state_ != State::LISTENING) {
        RCLCPP_INFO(get_logger(), "ASR barge-in, cancelling current turn");
        cancel_current_turn();
        transition(State::LISTENING);
    }
    RCLCPP_INFO(get_logger(), "ASR: %s", msg.data.c_str());
    reset_session_timer();

    session_.metrics().asr_at = std::chrono::steady_clock::now();

    image_capture_.kick_off();
    request_inference(Inference::Goal::TRIGGER_VOICE, msg.data);
}

void BrainNode::on_emotion(const buddy_interfaces::msg::EmotionResult& msg) {
    current_emotion_ = msg.emotion;
    emotion_confidence_ = msg.confidence;

    if (!emotion_trigger_ || state_ != State::IDLE) return;

    if (emotion_trigger_->update(msg.emotion, msg.confidence)) {
        RCLCPP_INFO(get_logger(), "Emotion trigger: %s (%.2f)", msg.emotion.c_str(), msg.confidence);
        transition(State::EMOTION_TRIGGER);
        request_inference(Inference::Goal::TRIGGER_EMOTION, "");
    }
}

void BrainNode::process_chunk_text(const std::string& chunk_text, bool is_final) {
    if (!chunk_text.empty()) {
        response_text_ += chunk_text;
        auto sentences = segmenter_.feed(chunk_text);
        for (auto& s : sentences) {
            if (sentence_index_ == 0) {
                session_.metrics().first_sentence_at = std::chrono::steady_clock::now();
            }
            auto sentence_msg = buddy_interfaces::msg::Sentence();
            sentence_msg.session_id = session_.session_id();
            sentence_msg.turn_id = session_.turn_id();
            sentence_msg.text = s;
            sentence_msg.index = sentence_index_++;
            sentence_pub_->publish(sentence_msg);
            if (state_ == State::SPEAKING) reset_speaking_watchdog();
        }
    }

    if (is_final) {
        flush_sentence_buffer();
        auto resp = std_msgs::msg::String();
        resp.data = response_text_;
        response_pub_->publish(resp);
    }
}

void BrainNode::on_llm_feedback(GoalHandleInference::SharedPtr /*goal_handle*/,
                                const std::shared_ptr<const Inference::Feedback> feedback) {
    if (state_ != State::REQUESTING) return;
    if (!llm_first_token_received_) {
        session_.metrics().llm_first_token_at = std::chrono::steady_clock::now();
        llm_first_token_received_ = true;
    }
    process_chunk_text(feedback->chunk_text, false);
}

void BrainNode::on_llm_result(const GoalHandleInference::WrappedResult& result) {
    llm_goal_handle_.reset();
    if (result.code == rclcpp_action::ResultCode::CANCELED) return;
    if (state_ != State::REQUESTING) return;

    session_.metrics().llm_result_at = std::chrono::steady_clock::now();

    const bool success = result.code == rclcpp_action::ResultCode::SUCCEEDED && result.result && result.result->success;

    if (success) {
        const std::string& full_response = result.result->full_response;
        const bool already_streamed = !response_text_.empty();
        if (!already_streamed && !full_response.empty()) {
            process_chunk_text(full_response, true);
        } else {
            process_chunk_text("", true);
        }
        last_assistant_response_ = already_streamed ? response_text_ : full_response;
        session_.add_assistant_response(last_assistant_response_);
        transition(State::SPEAKING);
    } else {
        RCLCPP_WARN(get_logger(), "LLM inference failed: %s",
                    result.result ? result.result->error_message.c_str() : "unknown");
        emit_fallback_sentence();
        transition(State::SPEAKING);
    }
}

void BrainNode::on_tts_done(const std_msgs::msg::Empty&) {
    if (state_ == State::SPEAKING) {
        last_tts_done_at_ = std::chrono::steady_clock::now();
        has_tts_done_timestamp_ = true;
        session_.metrics().tts_done_at = last_tts_done_at_;
        log_turn_metrics("complete");
        if (speaking_watchdog_) speaking_watchdog_->cancel();
        RCLCPP_INFO(get_logger(), "TTS done, returning to idle");
        transition(State::IDLE);
        reset_session_timer();
    }
}

// ---------------------------------------------------------------------------
// State machine helpers
// ---------------------------------------------------------------------------

void BrainNode::transition(State new_state) {
    static const char* names[] = {"IDLE", "LISTENING", "EMOTION_TRIGGER", "REQUESTING", "SPEAKING"};
    if (state_ == State::IDLE && emotion_trigger_) emotion_trigger_->reset();
    RCLCPP_INFO(get_logger(), "State: %s -> %s", names[static_cast<int>(state_)], names[static_cast<int>(new_state)]);
    state_ = new_state;

    if (new_state == State::SPEAKING) {
        reset_speaking_watchdog();
    } else if (speaking_watchdog_) {
        speaking_watchdog_->cancel();
    }
}

void BrainNode::request_inference(uint8_t trigger_type, const std::string& user_text) {
    auto pending_wake_at = session_.metrics().wake_at;
    auto pending_asr_at = session_.metrics().asr_at;
    session_.start_turn();
    session_.metrics().wake_at = pending_wake_at;
    session_.metrics().asr_at = pending_asr_at;

    segmenter_.reset();
    sentence_index_ = 0;
    response_text_.clear();
    llm_first_token_received_ = false;

    Inference::Goal goal;
    goal.session_id = session_.session_id();
    goal.turn_id = session_.turn_id();
    goal.trigger_type = trigger_type;
    goal.user_text = user_text;
    goal.emotion = current_emotion_;
    goal.emotion_confidence = emotion_confidence_;
    goal.system_prompt = session_.system_prompt();

    for (const auto& h : session_.history()) {
        goal.dialog_history.push_back(h);
    }

    if (!user_text.empty()) {
        session_.add_user_text(user_text);
    }

    auto image = image_capture_.wait_and_get();
    if (!image.data.empty()) {
        goal.image = image;
    }

    if (!llm_client_->action_server_is_ready()) {
        RCLCPP_WARN(get_logger(), "LLM inference server not ready");
        emit_fallback_sentence();
        transition(State::SPEAKING);
        return;
    }

    auto opts = rclcpp_action::Client<Inference>::SendGoalOptions();
    opts.goal_response_callback = [this](GoalHandleInference::SharedPtr handle) { llm_goal_handle_ = handle; };
    opts.feedback_callback =
        std::bind(&BrainNode::on_llm_feedback, this, std::placeholders::_1, std::placeholders::_2);
    opts.result_callback = std::bind(&BrainNode::on_llm_result, this, std::placeholders::_1);
    llm_client_->async_send_goal(goal, opts);

    transition(State::REQUESTING);
}

void BrainNode::emit_fallback_sentence() {
    auto sentence_msg = buddy_interfaces::msg::Sentence();
    sentence_msg.session_id = session_.session_id();
    sentence_msg.turn_id = session_.turn_id();
    sentence_msg.text = fallback_sentence_;
    last_assistant_response_ = sentence_msg.text;
    sentence_msg.index = sentence_index_++;
    sentence_msg.is_final = true;
    sentence_pub_->publish(sentence_msg);
}

void BrainNode::flush_sentence_buffer() {
    auto remainder = segmenter_.flush();
    auto s = buddy_interfaces::msg::Sentence();
    s.session_id = session_.session_id();
    s.turn_id = session_.turn_id();
    s.text = remainder;
    s.index = sentence_index_++;
    s.is_final = true;
    sentence_pub_->publish(s);
}

void BrainNode::reset_speaking_watchdog() {
    if (speaking_watchdog_) speaking_watchdog_->cancel();
    speaking_watchdog_ = create_wall_timer(std::chrono::milliseconds(watchdog_timeout_ms_), [this]() {
        RCLCPP_WARN(get_logger(), "SPEAKING watchdog timeout, forcing IDLE");
        speaking_watchdog_->cancel();
        log_turn_metrics("watchdog");
        state_ = State::IDLE;
        reset_session_timer();
    });
}

void BrainNode::reset_session_timer() {
    if (session_timer_) session_timer_->cancel();
    session_timer_ = create_wall_timer(std::chrono::duration<double>(session_timeout_seconds_), [this]() {
        if (state_ == State::SPEAKING) {
            reset_session_timer();
            return;
        }
        RCLCPP_INFO(get_logger(), "Session timed out, clearing context");
        session_.clear();
        session_timer_->cancel();
    });
}

void BrainNode::cancel_current_turn() {
    if (llm_goal_handle_) {
        llm_client_->async_cancel_goal(llm_goal_handle_);
        llm_goal_handle_.reset();
    }

    log_turn_metrics("barge-in");

    auto msg = buddy_interfaces::msg::Sentence();
    msg.session_id = session_.session_id();
    msg.turn_id = session_.turn_id();
    msg.is_final = true;
    msg.text = "";
    sentence_pub_->publish(msg);

    session_.remove_last_user_turn();
}

void BrainNode::log_turn_metrics(const char* reason) {
    RCLCPP_INFO(get_logger(), "[%s] %s", reason, session_.metrics().to_log_string().c_str());
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(BrainNode)
