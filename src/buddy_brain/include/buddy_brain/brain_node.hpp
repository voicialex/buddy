#pragma once

#include <buddy_interfaces/action/inference.hpp>
#include <buddy_interfaces/msg/emotion_result.hpp>
#include <buddy_interfaces/msg/sentence.hpp>
#include <buddy_interfaces/msg/tts_control.hpp>
#include <buddy_interfaces/srv/capture_image.hpp>
#include <chrono>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/empty.hpp>
#include <std_msgs/msg/string.hpp>
#include <string>
#include <vector>

#include "buddy_brain/asr_post_filter.hpp"
#include "buddy_brain/emotion_trigger.hpp"
#include "buddy_brain/image_capture_coordinator.hpp"
#include "buddy_brain/sentence_segmenter.hpp"
#include "buddy_brain/session_context.hpp"

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;
using Inference = buddy_interfaces::action::Inference;
using GoalHandleInference = rclcpp_action::ClientGoalHandle<Inference>;

class BrainNode : public rclcpp_lifecycle::LifecycleNode {
public:
    explicit BrainNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

    CallbackReturn on_configure(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_activate(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_deactivate(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_cleanup(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_shutdown(const rclcpp_lifecycle::State&) override;
    CallbackReturn on_error(const rclcpp_lifecycle::State&) override;

    enum class State { IDLE, LISTENING, EMOTION_TRIGGER, REQUESTING, SPEAKING };
    State state() const { return state_; }

private:
    void on_wake_word(const std_msgs::msg::String& msg);
    void on_asr_text(const std_msgs::msg::String& msg);
    void on_emotion(const buddy_interfaces::msg::EmotionResult& msg);
    void on_tts_done(const std_msgs::msg::Empty& msg);

    void on_llm_feedback(GoalHandleInference::SharedPtr goal_handle,
                         const std::shared_ptr<const Inference::Feedback> feedback);
    void on_llm_result(const GoalHandleInference::WrappedResult& result);

    void process_chunk_text(const std::string& chunk_text, bool is_final);
    void transition(State new_state);
    void request_inference(uint8_t trigger_type, const std::string& user_text);
    void emit_fallback_sentence();
    void flush_sentence_buffer();
    void reset_session_timer();
    void reset_speaking_watchdog();
    void cancel_current_turn();
    void log_turn_metrics(const char* reason);

    // State machine
    State state_{State::IDLE};
    rclcpp::TimerBase::SharedPtr session_timer_;
    rclcpp::TimerBase::SharedPtr speaking_watchdog_;
    double session_timeout_seconds_{60.0};
    int watchdog_timeout_ms_{60000};

    // Extracted components
    SessionContext session_;
    ImageCaptureCoordinator image_capture_;
    SentenceSegmenter segmenter_;
    std::unique_ptr<EmotionTrigger> emotion_trigger_;
    std::unique_ptr<AsrPostFilter> asr_filter_;

    // ASR filter context
    bool kws_enabled_{true};
    std::string last_assistant_response_;
    std::chrono::steady_clock::time_point last_tts_done_at_{};
    bool has_tts_done_timestamp_{false};
    std::vector<std::string> wake_phrase_fallbacks_;
    std::string fallback_sentence_;

    // Emotion state
    std::string current_emotion_;
    float emotion_confidence_{0.f};

    // Streaming response
    uint32_t sentence_index_{0};
    std::string response_text_;
    bool llm_first_token_received_{false};

    // LLM action client
    rclcpp_action::Client<Inference>::SharedPtr llm_client_;
    GoalHandleInference::SharedPtr llm_goal_handle_;

    // ROS pub/sub
    rclcpp::Publisher<buddy_interfaces::msg::Sentence>::SharedPtr sentence_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr response_pub_;
    rclcpp::Publisher<buddy_interfaces::msg::TtsControl>::SharedPtr tts_control_pub_;

    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr wake_word_sub_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr asr_text_sub_;
    rclcpp::Subscription<buddy_interfaces::msg::EmotionResult>::SharedPtr emotion_sub_;
    rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr tts_done_sub_;

    rclcpp::CallbackGroup::SharedPtr capture_client_group_;
};
