#include "buddy_brain/brain_node.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace trigger_types {
constexpr const char *kVoice = "voice";
constexpr const char *kEmotion = "emotion";
} // namespace trigger_types

BrainNode::BrainNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("brain", options) {}

CallbackReturn BrainNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "BrainNode: configuring");

  declare_parameter("system_prompt_path", "");
  declare_parameter("max_history_turns", 10);
  declare_parameter("emotion_trigger.enabled", true);
  declare_parameter("emotion_trigger.negative_emotions",
                    std::vector<std::string>{"sad", "angry", "fear"});
  declare_parameter("emotion_trigger.confidence_threshold", 0.7);
  declare_parameter("emotion_trigger.duration_seconds", 3.0);
  declare_parameter("emotion_trigger.cooldown_seconds", 60.0);
  declare_parameter("voice_trigger.attach_image", true);
  declare_parameter("session_timeout_seconds", 60.0);

  max_history_turns_ = get_parameter("max_history_turns").as_int();
  session_timeout_seconds_ =
      get_parameter("session_timeout_seconds").as_double();
  emotion_trigger_enabled_ = get_parameter("emotion_trigger.enabled").as_bool();
  negative_emotions_ =
      get_parameter("emotion_trigger.negative_emotions").as_string_array();
  emotion_confidence_threshold_ = static_cast<float>(
      get_parameter("emotion_trigger.confidence_threshold").as_double());
  emotion_duration_seconds_ =
      get_parameter("emotion_trigger.duration_seconds").as_double();
  emotion_cooldown_seconds_ =
      get_parameter("emotion_trigger.cooldown_seconds").as_double();
  voice_attach_image_ = get_parameter("voice_trigger.attach_image").as_bool();

  auto prompt_path = get_parameter("system_prompt_path").as_string();
  if (!prompt_path.empty()) {
    std::ifstream f(prompt_path);
    if (f.is_open()) {
      std::ostringstream ss;
      ss << f.rdbuf();
      system_prompt_ = ss.str();
    }
  }

  inference_request_pub_ =
      create_publisher<buddy_interfaces::msg::InferenceRequest>(
          "/brain/request", 10);
  sentence_pub_ =
      create_publisher<buddy_interfaces::msg::Sentence>("/brain/sentence", 10);
  response_pub_ =
      create_publisher<std_msgs::msg::String>("/brain/response", 10);

  wake_word_sub_ = create_subscription<std_msgs::msg::String>(
      "/audio/wake_word", 10,
      std::bind(&BrainNode::on_wake_word, this, std::placeholders::_1));
  asr_text_sub_ = create_subscription<std_msgs::msg::String>(
      "/audio/asr_text", 10,
      std::bind(&BrainNode::on_asr_text, this, std::placeholders::_1));
  emotion_sub_ = create_subscription<buddy_interfaces::msg::EmotionResult>(
      "/vision/emotion/result", 10,
      std::bind(&BrainNode::on_emotion, this, std::placeholders::_1));
  local_chunk_sub_ = create_subscription<buddy_interfaces::msg::InferenceChunk>(
      "/inference/local_chunk", 10,
      std::bind(&BrainNode::on_local_chunk, this, std::placeholders::_1));
  cloud_chunk_sub_ = create_subscription<buddy_interfaces::msg::InferenceChunk>(
      "/inference/cloud_chunk", 10,
      std::bind(&BrainNode::on_cloud_chunk, this, std::placeholders::_1));
  tts_done_sub_ = create_subscription<std_msgs::msg::Empty>(
      "/audio/tts_done", 10,
      std::bind(&BrainNode::on_tts_done, this, std::placeholders::_1));

  capture_client_ = create_client<buddy_interfaces::srv::CaptureImage>(
      "/vision/emotion/capture");

  return CallbackReturn::SUCCESS;
}

CallbackReturn BrainNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "BrainNode: activating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn BrainNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "BrainNode: deactivating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn BrainNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "BrainNode: cleaning up");
  inference_request_pub_.reset();
  sentence_pub_.reset();
  response_pub_.reset();
  wake_word_sub_.reset();
  asr_text_sub_.reset();
  emotion_sub_.reset();
  local_chunk_sub_.reset();
  cloud_chunk_sub_.reset();
  tts_done_sub_.reset();
  capture_client_.reset();
  history_.clear();
  sentence_buffer_.clear();
  sentence_index_ = 0;
  return CallbackReturn::SUCCESS;
}

CallbackReturn BrainNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "BrainNode: shutting down");
  return CallbackReturn::SUCCESS;
}

CallbackReturn BrainNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "BrainNode: error");
  return CallbackReturn::SUCCESS;
}

void BrainNode::on_wake_word(const std_msgs::msg::String &) {
  if (state_ != State::IDLE) {
    RCLCPP_INFO(get_logger(), "Wake word: barge-in, cancelling current turn");
    cancel_current_turn();
  }
  RCLCPP_INFO(get_logger(), "Wake word detected");
  reset_session_timer();
  transition(State::LISTENING);
}

void BrainNode::on_asr_text(const std_msgs::msg::String &msg) {
  if (state_ == State::IDLE) {
    transition(State::LISTENING);
  } else if (state_ != State::LISTENING) {
    RCLCPP_INFO(get_logger(), "ASR barge-in, cancelling current turn");
    cancel_current_turn();
    transition(State::LISTENING);
  }
  RCLCPP_INFO(get_logger(), "ASR: %s", msg.data.c_str());
  reset_session_timer();
  request_inference(trigger_types::kVoice, msg.data);
}

void BrainNode::on_emotion(const buddy_interfaces::msg::EmotionResult &msg) {
  current_emotion_ = msg.emotion;
  emotion_confidence_ = msg.confidence;

  if (!emotion_trigger_enabled_ || state_ != State::IDLE)
    return;

  bool is_negative =
      std::find(negative_emotions_.begin(), negative_emotions_.end(),
                msg.emotion) != negative_emotions_.end();

  auto now = std::chrono::steady_clock::now();

  if (is_negative && msg.confidence >= emotion_confidence_threshold_) {
    if (!tracking_negative_) {
      tracking_negative_ = true;
      negative_since_ = now;
    }
    auto elapsed = std::chrono::duration<double>(now - negative_since_).count();
    auto cooldown =
        std::chrono::duration<double>(now - last_proactive_trigger_).count();

    if (elapsed >= emotion_duration_seconds_ &&
        cooldown >= emotion_cooldown_seconds_) {
      RCLCPP_INFO(get_logger(), "Emotion trigger: %s (%.2f) for %.1fs",
                  msg.emotion.c_str(), msg.confidence, elapsed);
      last_proactive_trigger_ = now;
      tracking_negative_ = false;
      transition(State::EMOTION_TRIGGER);
      request_inference(trigger_types::kEmotion, "");
    }
  } else {
    tracking_negative_ = false;
  }
}

void BrainNode::process_chunk(
    const buddy_interfaces::msg::InferenceChunk &msg) {
  if (!msg.chunk_text.empty()) {
    response_text_ += msg.chunk_text;
    auto sentences = segment(msg.chunk_text);
    for (auto &s : sentences) {
      auto sentence_msg = buddy_interfaces::msg::Sentence();
      sentence_msg.session_id = session_id_;
      sentence_msg.turn_id = turn_id_;
      sentence_msg.text = s;
      sentence_msg.index = sentence_index_++;
      sentence_pub_->publish(sentence_msg);
    }
  }

  if (msg.is_final) {
    flush_sentence_buffer(session_id_);
    auto resp = std_msgs::msg::String();
    resp.data = response_text_;
    response_pub_->publish(resp);
  }
}

void BrainNode::on_local_chunk(
    const buddy_interfaces::msg::InferenceChunk &msg) {
  if (state_ != State::REQUESTING)
    return;
  // Ignore chunks from expired turns
  if (!msg.turn_id.empty() && msg.turn_id != turn_id_)
    return;
  process_chunk(msg);
  if (msg.is_final) {
    // Local is not authoritative; transition to SPEAKING so the cycle
    // completes if cloud never arrives, but do NOT write history.
    transition(State::SPEAKING);
  }
}

void BrainNode::on_cloud_chunk(
    const buddy_interfaces::msg::InferenceChunk &msg) {
  // Ignore chunks from expired turns
  if (!msg.turn_id.empty() && msg.turn_id != turn_id_)
    return;
  // Accept cloud in any post-request state (REQUESTING, SPEAKING, or even
  // IDLE when the TTS stub completes instantly before cloud arrives).
  if (state_ == State::LISTENING || state_ == State::EMOTION_TRIGGER)
    return;

  if (first_cloud_chunk_) {
    first_cloud_chunk_ = false;
    response_text_.clear();
    sentence_buffer_.clear();
    if (state_ != State::REQUESTING)
      transition(State::REQUESTING);
    RCLCPP_INFO(get_logger(), "Cloud response arrived, replacing local");
  }

  process_chunk(msg);

  if (msg.is_final) {
    // Cloud is the authoritative response source
    if (!response_text_.empty()) {
      history_.push_back("assistant: " + response_text_);
      trim_history();
    }
    transition(State::SPEAKING);
  }
}

void BrainNode::on_tts_done(const std_msgs::msg::Empty &) {
  if (state_ == State::SPEAKING) {
    if (speaking_watchdog_) speaking_watchdog_->cancel();
    RCLCPP_INFO(get_logger(), "TTS done, returning to idle");
    transition(State::IDLE);
  }
}

void BrainNode::transition(State new_state) {
  static const char *names[] = {"IDLE", "LISTENING", "EMOTION_TRIGGER",
                                "REQUESTING", "SPEAKING"};
  if (state_ == State::IDLE)
    tracking_negative_ = false;
  RCLCPP_INFO(get_logger(), "State: %s -> %s", names[static_cast<int>(state_)],
              names[static_cast<int>(new_state)]);
  state_ = new_state;

  // SPEAKING watchdog: safety net in case tts_done is lost
  if (new_state == State::SPEAKING) {
    speaking_watchdog_ = create_wall_timer(std::chrono::seconds(30), [this]() {
      RCLCPP_WARN(get_logger(), "SPEAKING watchdog timeout, forcing IDLE");
      speaking_watchdog_->cancel();
      state_ = State::IDLE;
    });
  } else if (speaking_watchdog_) {
    speaking_watchdog_->cancel();
  }
}

void BrainNode::request_inference(const std::string &trigger_type,
                                  const std::string &user_text) {
  // New session if none exists
  if (session_id_.empty()) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    session_id_ = "sess-" + std::to_string(now_ms);
    turn_counter_ = 0;
    history_.clear();
  }

  // New turn within the session
  turn_id_ = session_id_ + "-t" + std::to_string(++turn_counter_);
  sentence_buffer_.clear();
  sentence_index_ = 0;
  first_cloud_chunk_ = true;
  response_text_.clear();

  auto req = buddy_interfaces::msg::InferenceRequest();
  req.session_id = session_id_;
  req.turn_id = turn_id_;
  req.trigger_type = trigger_type;
  req.user_text = user_text;
  req.emotion = current_emotion_;
  req.emotion_confidence = emotion_confidence_;
  req.system_prompt = system_prompt_;

  for (auto &h : history_) {
    req.dialog_history.push_back(h);
  }

  if (!user_text.empty()) {
    history_.push_back("user: " + user_text);
    trim_history();
  }

  if (voice_attach_image_ || trigger_type == trigger_types::kEmotion) {
    if (capture_client_->service_is_ready()) {
      auto capture_req =
          std::make_shared<buddy_interfaces::srv::CaptureImage::Request>();
      capture_future_ =
          capture_client_->async_send_request(capture_req).share();
      RCLCPP_DEBUG(get_logger(), "Image capture requested");
    }
  }

  // Wait for image capture to complete before publishing
  if (capture_future_.valid()) {
    auto status = capture_future_.wait_for(std::chrono::milliseconds(500));
    if (status == std::future_status::ready) {
      auto result = capture_future_.get();
      if (result && !result->image.data.empty()) {
        req.image = result->image;
        RCLCPP_DEBUG(get_logger(), "Image attached to inference request");
      }
    } else {
      RCLCPP_WARN(get_logger(), "Image capture timed out, proceeding without image");
    }
  }

  inference_request_pub_->publish(req);
  transition(State::REQUESTING);
}

void BrainNode::flush_sentence_buffer(const std::string &session_id) {
  auto s = buddy_interfaces::msg::Sentence();
  s.session_id = session_id;
  s.turn_id = turn_id_;
  s.text = sentence_buffer_;
  s.index = sentence_index_++;
  s.is_final = true;
  sentence_pub_->publish(s);
  sentence_buffer_.clear();
}

void BrainNode::trim_history() {
  while (static_cast<int>(history_.size()) > max_history_turns_ * 2) {
    history_.pop_front();
  }
}

void BrainNode::reset_session_timer() {
  if (session_timer_) session_timer_->cancel();
  session_timer_ = create_wall_timer(
      std::chrono::duration<double>(session_timeout_seconds_),
      [this]() {
        RCLCPP_INFO(get_logger(), "Session timed out, clearing context");
        session_id_.clear();
        history_.clear();
        session_timer_->cancel();
      });
}

void BrainNode::cancel_current_turn() {
  // Send cancel signal to audio pipeline (empty sentence with current turn_id)
  auto msg = buddy_interfaces::msg::Sentence();
  msg.session_id = session_id_;
  msg.turn_id = turn_id_;
  msg.is_final = true;
  msg.text = "";
  sentence_pub_->publish(msg);
  // Roll back orphaned user history entry
  if (!history_.empty() &&
      history_.back().rfind("user: ", 0) == 0)
    history_.pop_back();
}

std::vector<std::string> BrainNode::segment(const std::string &text) {
  sentence_buffer_ += text;
  std::vector<std::string> result;
  size_t last = 0;
  static const std::vector<std::string> delimiters = {"\xe3\x80\x82", // 。
                                                      "\xef\xbc\x81", // ！
                                                      "\xef\xbc\x9f", // ？
                                                      ".",
                                                      "!",
                                                      "?"};

  while (last < sentence_buffer_.size()) {
    size_t best = std::string::npos;
    for (const auto &d : delimiters) {
      auto found = sentence_buffer_.find(d, last);
      if (found != std::string::npos &&
          (best == std::string::npos || found < best)) {
        best = found + d.size();
      }
    }
    if (best == std::string::npos)
      break;
    result.push_back(sentence_buffer_.substr(last, best - last));
    last = best;
  }

  sentence_buffer_ =
      (last < sentence_buffer_.size()) ? sentence_buffer_.substr(last) : "";

  return result;
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(BrainNode)
