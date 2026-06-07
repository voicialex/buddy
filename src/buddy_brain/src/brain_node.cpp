#include "buddy_brain/brain_node.hpp"

#include <algorithm>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace trigger_types {
constexpr const char* kVoice = "voice";
constexpr const char* kEmotion = "emotion";
}  // namespace trigger_types

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

std::unique_ptr<AsrFilter> make_asr_filter(const std::string& strategy, double guard_seconds, int min_chars) {
    if (strategy == "echo_substring") {
        return std::make_unique<EchoSubstringAsrFilter>(guard_seconds, min_chars);
    }
    if (strategy == "none") {
        return nullptr;
    }
    return {};
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

    max_history_turns_ = get_parameter("max_history_turns").as_int();
    session_timeout_seconds_ = get_parameter("session_timeout_seconds").as_double();
    watchdog_timeout_ms_ = static_cast<int>(get_parameter("speaking_watchdog_seconds").as_double() * 1000);
    emotion_trigger_enabled_ = get_parameter("emotion_trigger.enabled").as_bool();
    voice_attach_image_ = get_parameter("voice_trigger.attach_image").as_bool();
    voice_capture_timeout_ms_ = get_parameter("voice_trigger.capture_timeout_ms").as_int();
    if (voice_capture_timeout_ms_ < 0) voice_capture_timeout_ms_ = 0;
    followup_echo_guard_seconds_ = get_parameter("voice_trigger.followup_echo_guard_seconds").as_double();
    followup_echo_guard_min_chars_ = get_parameter("voice_trigger.followup_echo_guard_min_chars").as_int();
    const std::string asr_filter_strategy = get_parameter("voice_trigger.asr_filter.strategy").as_string();
    asr_filter_ = make_asr_filter(asr_filter_strategy, followup_echo_guard_seconds_, followup_echo_guard_min_chars_);
    if (asr_filter_strategy != "none" && !asr_filter_) {
        RCLCPP_ERROR(get_logger(),
                     "Invalid voice_trigger.asr_filter.strategy='%s'. Allowed: echo_substring | none",
                     asr_filter_strategy.c_str());
        return CallbackReturn::ERROR;
    }
    RCLCPP_INFO(get_logger(), "ASR filter strategy: %s", asr_filter_strategy.c_str());

    const WakePhraseLoadResult wake_phrase_load = load_wake_phrases_from_keywords_file();
    wake_phrase_fallbacks_ = wake_phrase_load.phrases;
    if (wake_phrase_fallbacks_.empty()) {
        wake_phrase_fallbacks_ = {"嘿巴迪"};
        RCLCPP_WARN(get_logger(), "No keywords.txt found, fallback wake phrase=%s", wake_phrase_fallbacks_[0].c_str());
    }

    // Emotion trigger
    EmotionTrigger::Config emo_cfg;
    emo_cfg.negative_emotions = get_parameter("emotion_trigger.negative_emotions").as_string_array();
    emo_cfg.confidence_threshold =
        static_cast<float>(get_parameter("emotion_trigger.confidence_threshold").as_double());
    emo_cfg.duration_seconds = get_parameter("emotion_trigger.duration_seconds").as_double();
    emo_cfg.cooldown_seconds = get_parameter("emotion_trigger.cooldown_seconds").as_double();
    emotion_trigger_ = std::make_unique<EmotionTrigger>(emo_cfg);

    // System prompt
    auto prompt_path = get_parameter("system_prompt_path").as_string();
    if (!prompt_path.empty()) {
        std::filesystem::path p(prompt_path);
        if (p.is_relative()) {
            try {
                auto share_dir = ament_index_cpp::get_package_share_directory("buddy_app");
                p = std::filesystem::path(share_dir) / prompt_path;
            } catch (...) {
                p = prompt_path;
            }
        }
        std::ifstream f(p);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            system_prompt_ = ss.str();
        }
    }

    // Single unified action client — Python LLM service via bridge
    llm_client_ = rclcpp_action::create_client<Inference>(this, "/inference/llm");

    sentence_pub_ = create_publisher<buddy_interfaces::msg::Sentence>("/brain/sentence", 10);
    response_pub_ = create_publisher<std_msgs::msg::String>("/brain/response", 10);
    tts_control_pub_ = create_publisher<buddy_interfaces::msg::TtsControl>("/brain/tts_control", 10);

    wake_word_sub_ = create_subscription<std_msgs::msg::String>(
        "/audio/wake_word", 10, std::bind(&BrainNode::on_wake_word, this, std::placeholders::_1));
    asr_text_sub_ = create_subscription<std_msgs::msg::String>(
        "/audio/asr_text", 10, std::bind(&BrainNode::on_asr_text, this, std::placeholders::_1));
    emotion_sub_ = create_subscription<buddy_interfaces::msg::EmotionResult>(
        "/vision/emotion/result", 10, std::bind(&BrainNode::on_emotion, this, std::placeholders::_1));
    tts_done_sub_ = create_subscription<std_msgs::msg::Empty>(
        "/audio/tts_done", 10, std::bind(&BrainNode::on_tts_done, this, std::placeholders::_1));

    // Keep capture client callbacks off the default mutually-exclusive group.
    // Otherwise waiting for capture future in ASR callback can starve service response handling.
    capture_client_group_ = create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    capture_client_ = create_client<buddy_interfaces::srv::CaptureImage>(
        "/vision/game/capture",
        rmw_qos_profile_services_default,
        capture_client_group_);

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
    capture_client_.reset();
    capture_client_group_.reset();
    history_.clear();
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

void BrainNode::on_wake_word(const std_msgs::msg::String& msg) {
    if (msg.data == "__no_kws__") {
        kws_enabled_ = false;
    }
    if (state_ != State::IDLE) {
        RCLCPP_INFO(get_logger(), "Wake word: barge-in, cancelling current turn");
        cancel_current_turn();
    }
    RCLCPP_INFO(get_logger(), "Wake word detected");
    reset_session_timer();
    transition(State::LISTENING);
}

void BrainNode::on_asr_text(const std_msgs::msg::String& msg) {
    const bool has_active_session = !session_id_.empty();
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

    // ASR noise filter strategy (replaceable)
    if (asr_filter_) {
        const double elapsed = has_tts_done_timestamp_
            ? std::chrono::duration<double>(std::chrono::steady_clock::now() - last_tts_done_at_).count()
            : -1.0;
        AsrFilterContext filter_ctx{};
        filter_ctx.is_idle = (state_ == State::IDLE);
        filter_ctx.has_active_session = has_active_session;
        filter_ctx.has_tts_done_timestamp = has_tts_done_timestamp_;
        filter_ctx.elapsed_since_tts_done_sec = elapsed;
        filter_ctx.last_assistant_response = last_assistant_response_;
        std::string reason;
        if (asr_filter_->should_filter(msg.data, filter_ctx, &reason)) {
            if (reason.empty()) reason = "ASR filtered by active noise strategy";
            RCLCPP_INFO(get_logger(), "%s", reason.c_str());
            return;
        }
    }

    if (state_ == State::IDLE) {
        transition(State::LISTENING);
    } else if (state_ != State::LISTENING) {
        RCLCPP_INFO(get_logger(), "ASR barge-in, cancelling current turn");
        cancel_current_turn();
        transition(State::LISTENING);
    }
    RCLCPP_INFO(get_logger(), "ASR: %s", msg.data.c_str());
    reset_session_timer();

    // Kick off game camera capture immediately (parallel with routing decision)
    if (voice_attach_image_) {
        if (capture_client_->service_is_ready()) {
            auto capture_req = std::make_shared<buddy_interfaces::srv::CaptureImage::Request>();
            capture_future_ = capture_client_->async_send_request(capture_req).share();
        } else {
            RCLCPP_INFO(get_logger(), "Game camera not available, proceeding without image");
        }
    }

    request_inference(trigger_types::kVoice, msg.data);
}

void BrainNode::on_emotion(const buddy_interfaces::msg::EmotionResult& msg) {
    current_emotion_ = msg.emotion;
    emotion_confidence_ = msg.confidence;

    if (!emotion_trigger_enabled_ || state_ != State::IDLE) return;

    if (emotion_trigger_->update(msg.emotion, msg.confidence)) {
        RCLCPP_INFO(get_logger(), "Emotion trigger: %s (%.2f)", msg.emotion.c_str(), msg.confidence);
        transition(State::EMOTION_TRIGGER);
        request_inference(trigger_types::kEmotion, "");
    }
}

void BrainNode::process_chunk_text(const std::string& chunk_text, bool is_final) {
    if (!chunk_text.empty()) {
        response_text_ += chunk_text;
        auto sentences = segmenter_.feed(chunk_text);
        for (auto& s : sentences) {
            auto sentence_msg = buddy_interfaces::msg::Sentence();
            sentence_msg.session_id = session_id_;
            sentence_msg.turn_id = turn_id_;
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
    process_chunk_text(feedback->chunk_text, false);
}

void BrainNode::on_llm_result(const GoalHandleInference::WrappedResult& result) {
    llm_goal_handle_.reset();
    if (result.code == rclcpp_action::ResultCode::CANCELED) return;
    if (state_ != State::REQUESTING) return;

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
        history_.push_back("assistant: " + last_assistant_response_);
        trim_history();
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
        if (speaking_watchdog_) speaking_watchdog_->cancel();
        RCLCPP_INFO(get_logger(), "TTS done, returning to idle");
        transition(State::IDLE);
    }
}

void BrainNode::transition(State new_state) {
    static const char* names[] = {"IDLE", "LISTENING", "EMOTION_TRIGGER", "REQUESTING", "SPEAKING"};
    if (state_ == State::IDLE) emotion_trigger_->reset();
    RCLCPP_INFO(get_logger(), "State: %s -> %s", names[static_cast<int>(state_)], names[static_cast<int>(new_state)]);
    state_ = new_state;

    if (new_state == State::SPEAKING) {
        reset_speaking_watchdog();
    } else if (speaking_watchdog_) {
        speaking_watchdog_->cancel();
    }
}

void BrainNode::request_inference(const std::string& trigger_type, const std::string& user_text) {
    if (session_id_.empty()) {
        auto now_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        session_id_ = "sess-" + std::to_string(now_ms);
        turn_counter_ = 0;
        history_.clear();
    }

    turn_id_ = session_id_ + "-t" + std::to_string(++turn_counter_);
    segmenter_.reset();
    sentence_index_ = 0;
    response_text_.clear();

    // Build goal
    Inference::Goal goal;
    goal.session_id = session_id_;
    goal.turn_id = turn_id_;
    goal.trigger_type = trigger_type;
    goal.user_text = user_text;
    goal.emotion = current_emotion_;
    goal.emotion_confidence = emotion_confidence_;
    goal.system_prompt = system_prompt_;

    for (auto& h : history_) {
        goal.dialog_history.push_back(h);
    }

    if (!user_text.empty()) {
        history_.push_back("user: " + user_text);
        trim_history();
    }

    // Attach image (future was kicked off in on_asr_text, just wait for result)
    if (capture_future_.valid()) {
        auto status = capture_future_.wait_for(std::chrono::milliseconds(voice_capture_timeout_ms_));
        if (status == std::future_status::ready) {
            auto res = capture_future_.get();
            if (res && !res->image.data.empty()) {
                goal.image = res->image;
            }
        } else {
            RCLCPP_WARN(get_logger(),
                        "Image capture timed out (%dms), proceeding without image (camera service ready=%s)",
                        voice_capture_timeout_ms_,
                        capture_client_ && capture_client_->service_is_ready() ? "yes" : "no");
        }
        capture_future_ = {};
    }

    // Send to unified LLM bridge
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
    sentence_msg.session_id = session_id_;
    sentence_msg.turn_id = turn_id_;
    sentence_msg.text = "抱歉，我现在无法回答";
    last_assistant_response_ = sentence_msg.text;
    sentence_msg.index = sentence_index_++;
    sentence_msg.is_final = true;
    sentence_pub_->publish(sentence_msg);
}

void BrainNode::flush_sentence_buffer() {
    auto remainder = segmenter_.flush();
    auto s = buddy_interfaces::msg::Sentence();
    s.session_id = session_id_;
    s.turn_id = turn_id_;
    s.text = remainder;
    s.index = sentence_index_++;
    s.is_final = true;
    sentence_pub_->publish(s);
}

void BrainNode::trim_history() {
    while (static_cast<int>(history_.size()) > max_history_turns_ * 2) {
        history_.pop_front();
    }
}

void BrainNode::reset_speaking_watchdog() {
    if (speaking_watchdog_) speaking_watchdog_->cancel();
    speaking_watchdog_ = create_wall_timer(std::chrono::milliseconds(watchdog_timeout_ms_), [this]() {
        RCLCPP_WARN(get_logger(), "SPEAKING watchdog timeout, forcing IDLE");
        speaking_watchdog_->cancel();
        state_ = State::IDLE;
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
        session_id_.clear();
        history_.clear();
        session_timer_->cancel();
    });
}

void BrainNode::cancel_current_turn() {
    if (llm_goal_handle_) {
        llm_client_->async_cancel_goal(llm_goal_handle_);
        llm_goal_handle_.reset();
    }

    auto msg = buddy_interfaces::msg::Sentence();
    msg.session_id = session_id_;
    msg.turn_id = turn_id_;
    msg.is_final = true;
    msg.text = "";
    sentence_pub_->publish(msg);

    if (!history_.empty() && history_.back().rfind("user: ", 0) == 0) {
        history_.pop_back();
    }
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(BrainNode)
