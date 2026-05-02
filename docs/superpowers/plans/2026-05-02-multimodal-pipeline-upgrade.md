# Buddy 多模态管线升级 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Simplify from 7 packages to 4 (audio, vision, brain, cloud) and upgrade the pipeline with mixed-trigger arbitration, multimodal cloud requests (Doubao API), while keeping the system buildable at every step.

**Architecture:** Delete buddy_dialog (dead code), buddy_sentence (inline into brain), buddy_state_machine (merge into brain). Create buddy_brain as the central orchestrator handling state machine, dialog context, and sentence segmentation. Upgrade buddy_cloud from stub to Doubao multimodal API. Audio ASR replacement (Sherpa-ONNX) is a separate parallel track.

**Tech Stack:** C++17, ROS 2 Humble, rclcpp_lifecycle, rclcpp_components, ONNX Runtime, libcurl (for cloud HTTP), Sherpa-ONNX (for ASR)

**Spec:** `docs/superpowers/specs/2026-05-02-multimodal-pipeline-upgrade-design.md` v2.0

---

## File Structure

### New files (buddy_brain)
- `src/buddy_brain/CMakeLists.txt` — build config
- `src/buddy_brain/package.xml` — package manifest
- `src/buddy_brain/include/buddy_brain/brain_node.hpp` — BrainNode class declaration
- `src/buddy_brain/src/brain_node.cpp` — state machine + dialog context + sentence segmentation
- `src/buddy_brain/test/test_brain_node.cpp` — lifecycle + state transition tests
- `src/buddy_brain/test/test_segment.cpp` — sentence segmentation unit tests

### New files (interfaces)
- `src/buddy_interfaces/msg/CloudRequest.msg` — brain → cloud multimodal request

### Modified files
- `src/buddy_app/src/buddy_main.cpp` — remove 3 old components, add brain
- `src/buddy_app/params/modules.yaml` — remove old entries, add brain
- `src/buddy_app/params/brain.yaml` — new config for brain
- `src/buddy_cloud/include/buddy_cloud/cloud_client_node.hpp` — subscribe to CloudRequest
- `src/buddy_cloud/src/cloud_client_node.cpp` — Doubao multimodal API integration
- `src/buddy_cloud/CMakeLists.txt` — add libcurl dependency
- `src/buddy_cloud/package.xml` — add sensor_msgs dependency
- `src/buddy_app/params/cloud.yaml` — Doubao config

### Deleted directories
- `src/buddy_dialog/` — entire package
- `src/buddy_sentence/` — entire package
- `src/buddy_state_machine/` — entire package

---

## Phase 0: Architecture Simplification

### Task 1: Add CloudRequest message to buddy_interfaces

**Files:**
- Create: `src/buddy_interfaces/msg/CloudRequest.msg`
- Modify: `src/buddy_interfaces/CMakeLists.txt`

- [ ] **Step 1: Create CloudRequest.msg**

```
# CloudRequest.msg — brain → cloud
string trigger_type           # "voice" or "emotion"
string user_text              # ASR text (voice trigger)
string emotion                # current emotion label
float32 emotion_confidence    # emotion confidence
string[] dialog_history       # recent N turns
string system_prompt          # system prompt text
sensor_msgs/Image image       # camera snapshot (optional)
```

Write to `src/buddy_interfaces/msg/CloudRequest.msg`.

- [ ] **Step 2: Register CloudRequest in CMakeLists.txt**

In `src/buddy_interfaces/CMakeLists.txt`, add `"msg/CloudRequest.msg"` to the `rosidl_generate_interfaces` call, right after the existing messages. Also ensure `sensor_msgs` is in `find_package` and `rosidl_generate_interfaces DEPENDENCIES`.

- [ ] **Step 3: Build buddy_interfaces to verify**

Run: `./build.sh --packages-select buddy_interfaces`
Expected: Clean build, CloudRequest header generated in output.

- [ ] **Step 4: Commit**

```bash
git add src/buddy_interfaces/msg/CloudRequest.msg src/buddy_interfaces/CMakeLists.txt
git commit -m "feat(module): [PRO-10000] Add CloudRequest message for brain-to-cloud multimodal"
```

---

### Task 2: Create buddy_brain package scaffold

**Files:**
- Create: `src/buddy_brain/package.xml`
- Create: `src/buddy_brain/CMakeLists.txt`
- Create: `src/buddy_brain/include/buddy_brain/brain_node.hpp`
- Create: `src/buddy_brain/src/brain_node.cpp`

- [ ] **Step 1: Create package.xml**

```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>buddy_brain</name>
  <version>0.1.0</version>
  <description>Central orchestrator: state machine, dialog context, sentence segmentation</description>
  <maintainer email="dev@buddy.ai">buddy</maintainer>
  <license>MIT</license>

  <buildtool_depend>ament_cmake</buildtool_depend>
  <depend>rclcpp</depend>
  <depend>rclcpp_lifecycle</depend>
  <depend>rclcpp_components</depend>
  <depend>std_msgs</depend>
  <depend>sensor_msgs</depend>
  <depend>buddy_interfaces</depend>
  <test_depend>ament_cmake_gtest</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

Write to `src/buddy_brain/package.xml`.

- [ ] **Step 2: Create CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_brain)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_library(brain_component SHARED src/brain_node.cpp)
target_include_directories(
  brain_component
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_compile_features(brain_component PUBLIC cxx_std_17)
ament_target_dependencies(
  brain_component
  rclcpp
  rclcpp_lifecycle
  rclcpp_components
  std_msgs
  sensor_msgs
  buddy_interfaces)
rclcpp_components_register_nodes(brain_component "BrainNode")
install(
  TARGETS brain_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_brain_node test/test_brain_node.cpp src/brain_node.cpp)
  target_include_directories(
    test_brain_node
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_brain_node PUBLIC cxx_std_17)
  ament_target_dependencies(
    test_brain_node
    rclcpp
    rclcpp_lifecycle
    rclcpp_components
    std_msgs
    sensor_msgs
    buddy_interfaces)

  ament_add_gtest(test_segment test/test_segment.cpp src/brain_node.cpp)
  target_include_directories(
    test_segment
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_segment PUBLIC cxx_std_17)
  ament_target_dependencies(
    test_segment
    rclcpp
    rclcpp_lifecycle
    rclcpp_components
    std_msgs
    sensor_msgs
    buddy_interfaces)
endif()
ament_package()
```

Write to `src/buddy_brain/CMakeLists.txt`.

- [ ] **Step 3: Create brain_node.hpp**

```cpp
#pragma once

#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/cloud_request.hpp>
#include <buddy_interfaces/msg/emotion_result.hpp>
#include <buddy_interfaces/msg/sentence.hpp>
#include <buddy_interfaces/srv/capture_image.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/empty.hpp>
#include <std_msgs/msg/string.hpp>

#include <chrono>
#include <deque>
#include <string>
#include <vector>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class BrainNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit BrainNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

  // Public for testing
  enum class State { IDLE, LISTENING, EMOTION_TRIGGER, REQUESTING, SPEAKING };
  State state() const { return state_; }
  std::vector<std::string> segment(const std::string &text);

private:
  // Callbacks
  void on_wake_word(const std_msgs::msg::String &msg);
  void on_asr_text(const std_msgs::msg::String &msg);
  void on_emotion(const buddy_interfaces::msg::EmotionResult &msg);
  void on_cloud_chunk(const buddy_interfaces::msg::CloudChunk &msg);
  void on_tts_done(const std_msgs::msg::Empty &msg);

  // Actions
  void transition(State new_state);
  void request_cloud(const std::string &trigger_type,
                     const std::string &user_text);
  void flush_sentence_buffer(const std::string &session_id);

  // State machine
  State state_{State::IDLE};
  std::string session_id_;

  // Dialog context
  std::deque<std::string> history_;
  int max_history_turns_{10};
  std::string system_prompt_;

  // Emotion tracking
  std::string current_emotion_;
  float emotion_confidence_{0.f};
  std::chrono::steady_clock::time_point negative_since_;
  std::chrono::steady_clock::time_point last_proactive_trigger_;
  bool tracking_negative_{false};

  // Emotion trigger config
  bool emotion_trigger_enabled_{true};
  std::vector<std::string> negative_emotions_{"sad", "angry", "fear"};
  float emotion_confidence_threshold_{0.7f};
  double emotion_duration_seconds_{3.0};
  double emotion_cooldown_seconds_{60.0};
  bool voice_attach_image_{true};

  // Sentence segmentation buffer
  std::string sentence_buffer_;
  uint32_t sentence_index_{0};

  // ROS interfaces
  rclcpp::Publisher<buddy_interfaces::msg::CloudRequest>::SharedPtr
      cloud_request_pub_;
  rclcpp::Publisher<buddy_interfaces::msg::Sentence>::SharedPtr sentence_pub_;

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr wake_word_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr asr_text_sub_;
  rclcpp::Subscription<buddy_interfaces::msg::EmotionResult>::SharedPtr
      emotion_sub_;
  rclcpp::Subscription<buddy_interfaces::msg::CloudChunk>::SharedPtr
      cloud_chunk_sub_;
  rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr tts_done_sub_;

  rclcpp::Client<buddy_interfaces::srv::CaptureImage>::SharedPtr
      capture_client_;
};
```

Write to `src/buddy_brain/include/buddy_brain/brain_node.hpp`.

- [ ] **Step 4: Create brain_node.cpp with minimal lifecycle + migrated state machine + inlined segmentation**

```cpp
#include "buddy_brain/brain_node.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

BrainNode::BrainNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("brain", options) {}

CallbackReturn BrainNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "BrainNode: configuring");

  // Read parameters
  declare_parameter("system_prompt_path", "");
  declare_parameter("max_history_turns", 10);
  declare_parameter("emotion_trigger.enabled", true);
  declare_parameter("emotion_trigger.negative_emotions",
                    std::vector<std::string>{"sad", "angry", "fear"});
  declare_parameter("emotion_trigger.confidence_threshold", 0.7);
  declare_parameter("emotion_trigger.duration_seconds", 3.0);
  declare_parameter("emotion_trigger.cooldown_seconds", 60.0);
  declare_parameter("voice_trigger.attach_image", true);

  max_history_turns_ = get_parameter("max_history_turns").as_int();
  emotion_trigger_enabled_ =
      get_parameter("emotion_trigger.enabled").as_bool();
  negative_emotions_ =
      get_parameter("emotion_trigger.negative_emotions").as_string_array();
  emotion_confidence_threshold_ = static_cast<float>(
      get_parameter("emotion_trigger.confidence_threshold").as_double());
  emotion_duration_seconds_ =
      get_parameter("emotion_trigger.duration_seconds").as_double();
  emotion_cooldown_seconds_ =
      get_parameter("emotion_trigger.cooldown_seconds").as_double();
  voice_attach_image_ =
      get_parameter("voice_trigger.attach_image").as_bool();

  // Load system prompt from file
  auto prompt_path = get_parameter("system_prompt_path").as_string();
  if (!prompt_path.empty()) {
    std::ifstream f(prompt_path);
    if (f.is_open()) {
      std::ostringstream ss;
      ss << f.rdbuf();
      system_prompt_ = ss.str();
    }
  }

  // Publishers
  cloud_request_pub_ =
      create_publisher<buddy_interfaces::msg::CloudRequest>(
          "/brain/cloud_request", 10);
  sentence_pub_ =
      create_publisher<buddy_interfaces::msg::Sentence>("/brain/sentence", 10);

  // Subscriptions
  wake_word_sub_ = create_subscription<std_msgs::msg::String>(
      "/audio/wake_word", 10,
      std::bind(&BrainNode::on_wake_word, this, std::placeholders::_1));
  asr_text_sub_ = create_subscription<std_msgs::msg::String>(
      "/audio/asr_text", 10,
      std::bind(&BrainNode::on_asr_text, this, std::placeholders::_1));
  emotion_sub_ = create_subscription<buddy_interfaces::msg::EmotionResult>(
      "/vision/emotion/result", 10,
      std::bind(&BrainNode::on_emotion, this, std::placeholders::_1));
  cloud_chunk_sub_ = create_subscription<buddy_interfaces::msg::CloudChunk>(
      "/cloud/response", 10,
      std::bind(&BrainNode::on_cloud_chunk, this, std::placeholders::_1));
  tts_done_sub_ = create_subscription<std_msgs::msg::Empty>(
      "/audio/tts_done", 10,
      std::bind(&BrainNode::on_tts_done, this, std::placeholders::_1));

  // Service client for camera capture
  capture_client_ =
      create_client<buddy_interfaces::srv::CaptureImage>(
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
  cloud_request_pub_.reset();
  sentence_pub_.reset();
  wake_word_sub_.reset();
  asr_text_sub_.reset();
  emotion_sub_.reset();
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

// ---------------------------------------------------------------------------
// State machine callbacks
// ---------------------------------------------------------------------------

void BrainNode::on_wake_word(const std_msgs::msg::String &) {
  if (state_ != State::IDLE)
    return;
  RCLCPP_INFO(get_logger(), "Wake word detected");
  transition(State::LISTENING);
}

void BrainNode::on_asr_text(const std_msgs::msg::String &msg) {
  if (state_ != State::LISTENING)
    return;
  RCLCPP_INFO(get_logger(), "ASR: %s", msg.data.c_str());
  request_cloud("voice", msg.data);
}

void BrainNode::on_emotion(
    const buddy_interfaces::msg::EmotionResult &msg) {
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
    auto elapsed =
        std::chrono::duration<double>(now - negative_since_).count();
    auto cooldown =
        std::chrono::duration<double>(now - last_proactive_trigger_).count();

    if (elapsed >= emotion_duration_seconds_ &&
        cooldown >= emotion_cooldown_seconds_) {
      RCLCPP_INFO(get_logger(),
                  "Emotion trigger: %s (%.2f) for %.1fs",
                  msg.emotion.c_str(), msg.confidence, elapsed);
      last_proactive_trigger_ = now;
      tracking_negative_ = false;
      transition(State::EMOTION_TRIGGER);
      request_cloud("emotion", "");
    }
  } else {
    tracking_negative_ = false;
  }
}

void BrainNode::on_cloud_chunk(
    const buddy_interfaces::msg::CloudChunk &msg) {
  if (state_ != State::REQUESTING && state_ != State::SPEAKING)
    return;

  if (state_ == State::REQUESTING) {
    transition(State::SPEAKING);
  }

  // Segment streaming text into sentences
  auto sentences = segment(msg.chunk_text);
  for (auto &s : sentences) {
    auto sentence_msg = buddy_interfaces::msg::Sentence();
    sentence_msg.session_id = session_id_;
    sentence_msg.text = s;
    sentence_msg.index = sentence_index_++;
    sentence_pub_->publish(sentence_msg);
  }

  if (msg.is_final) {
    flush_sentence_buffer(session_id_);
    // Store assistant response in history
    // (accumulated from chunks — simplified: store final chunk)
    if (!msg.chunk_text.empty()) {
      history_.push_back("assistant: " + msg.chunk_text);
      while (static_cast<int>(history_.size()) > max_history_turns_ * 2) {
        history_.pop_front();
      }
    }
  }
}

void BrainNode::on_tts_done(const std_msgs::msg::Empty &) {
  if (state_ == State::SPEAKING) {
    RCLCPP_INFO(get_logger(), "TTS done, returning to idle");
    transition(State::IDLE);
  }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void BrainNode::transition(State new_state) {
  static const char *names[] = {"IDLE", "LISTENING", "EMOTION_TRIGGER",
                                "REQUESTING", "SPEAKING"};
  RCLCPP_INFO(get_logger(), "State: %s -> %s",
              names[static_cast<int>(state_)],
              names[static_cast<int>(new_state)]);
  state_ = new_state;
}

void BrainNode::request_cloud(const std::string &trigger_type,
                              const std::string &user_text) {
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  session_id_ = "sess-" + std::to_string(now_ms);
  sentence_buffer_.clear();
  sentence_index_ = 0;

  auto req = buddy_interfaces::msg::CloudRequest();
  req.trigger_type = trigger_type;
  req.user_text = user_text;
  req.emotion = current_emotion_;
  req.emotion_confidence = emotion_confidence_;
  req.system_prompt = system_prompt_;

  // Dialog history
  for (auto &h : history_) {
    req.dialog_history.push_back(h);
  }

  // Store user text in history
  if (!user_text.empty()) {
    history_.push_back("user: " + user_text);
    while (static_cast<int>(history_.size()) > max_history_turns_ * 2) {
      history_.pop_front();
    }
  }

  // Capture image if configured
  if (voice_attach_image_ || trigger_type == "emotion") {
    if (capture_client_->service_is_ready()) {
      auto capture_req =
          std::make_shared<buddy_interfaces::srv::CaptureImage::Request>();
      auto future = capture_client_->async_send_request(capture_req);
      // Non-blocking: use shared_future callback
      auto shared = future.share();
      // For now, publish without waiting for image
      // TODO: integrate async image capture with request
      RCLCPP_DEBUG(get_logger(), "Image capture requested");
    }
  }

  cloud_request_pub_->publish(req);
  transition(State::REQUESTING);
}

void BrainNode::flush_sentence_buffer(const std::string &session_id) {
  if (sentence_buffer_.empty())
    return;
  auto s = buddy_interfaces::msg::Sentence();
  s.session_id = session_id;
  s.text = sentence_buffer_;
  s.index = sentence_index_++;
  sentence_pub_->publish(s);
  sentence_buffer_.clear();
}

// ---------------------------------------------------------------------------
// Sentence segmentation (migrated from buddy_sentence)
// ---------------------------------------------------------------------------

std::vector<std::string> BrainNode::segment(const std::string &text) {
  sentence_buffer_ += text;
  std::vector<std::string> result;
  size_t last = 0;
  const std::vector<std::string> delimiters = {
      "\xe3\x80\x82", // 。
      "\xef\xbc\x81", // ！
      "\xef\xbc\x9f", // ？
      ".", "!", "?"};

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

  // Keep remainder in buffer
  sentence_buffer_ = (last < sentence_buffer_.size())
                         ? sentence_buffer_.substr(last)
                         : "";

  return result;
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(BrainNode)
```

Write to `src/buddy_brain/src/brain_node.cpp`.

- [ ] **Step 5: Build buddy_brain to verify compilation**

Run: `./build.sh --packages-select buddy_interfaces buddy_brain`
Expected: Clean build.

- [ ] **Step 6: Commit**

```bash
git add src/buddy_brain/
git commit -m "feat(module): [PRO-10000] Add buddy_brain package with state machine, dialog context, segmentation"
```

---

### Task 3: Write tests for buddy_brain

**Files:**
- Create: `src/buddy_brain/test/test_brain_node.cpp`
- Create: `src/buddy_brain/test/test_segment.cpp`

- [ ] **Step 1: Write lifecycle tests**

```cpp
// test_brain_node.cpp
#include "buddy_brain/brain_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class BrainNodeTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<BrainNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<BrainNode> node_;
};

TEST_F(BrainNodeTest, NodeName) {
  EXPECT_EQ(std::string(node_->get_name()), "brain");
}

TEST_F(BrainNodeTest, InitialStateIsIdle) {
  EXPECT_EQ(node_->state(), BrainNode::State::IDLE);
}

TEST_F(BrainNodeTest, ConfigureTransition) {
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
}

TEST_F(BrainNodeTest, FullLifecycleSequence) {
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
  EXPECT_STREQ(node_->activate().label().c_str(), "active");
  EXPECT_STREQ(node_->deactivate().label().c_str(), "inactive");
  EXPECT_STREQ(node_->cleanup().label().c_str(), "unconfigured");
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
```

Write to `src/buddy_brain/test/test_brain_node.cpp`.

- [ ] **Step 2: Write sentence segmentation tests**

```cpp
// test_segment.cpp
#include "buddy_brain/brain_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class SegmentTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<BrainNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<BrainNode> node_;
};

TEST_F(SegmentTest, SplitEnglishSentences) {
  auto result = node_->segment("Hello world. How are you? Fine!");
  EXPECT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0], "Hello world.");
  EXPECT_EQ(result[1], " How are you?");
  EXPECT_EQ(result[2], " Fine!");
}

TEST_F(SegmentTest, SplitChineseSentences) {
  auto result = node_->segment("你好世界。你好吗？很好！");
  EXPECT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0], "你好世界。");
  EXPECT_EQ(result[1], "你好吗？");
  EXPECT_EQ(result[2], "很好！");
}

TEST_F(SegmentTest, BuffersIncompleteText) {
  auto result = node_->segment("Hello world");
  EXPECT_EQ(result.size(), 0u);  // no delimiter, stays in buffer
  auto result2 = node_->segment(". More text.");
  EXPECT_EQ(result2.size(), 2u);
  EXPECT_EQ(result2[0], "Hello world.");
  EXPECT_EQ(result2[1], " More text.");
}

TEST_F(SegmentTest, EmptyInput) {
  auto result = node_->segment("");
  EXPECT_EQ(result.size(), 0u);
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
```

Write to `src/buddy_brain/test/test_segment.cpp`.

- [ ] **Step 3: Run tests**

Run: `./build.sh --packages-select buddy_interfaces buddy_brain && colcon test --packages-select buddy_brain && colcon test-result --verbose`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/buddy_brain/test/
git commit -m "feat(module): [PRO-10000] Add brain node lifecycle and segmentation tests"
```

---

### Task 4: Wire buddy_brain into buddy_main and remove old packages

**Files:**
- Modify: `src/buddy_app/src/buddy_main.cpp:23-38` (kComponents array)
- Modify: `src/buddy_app/params/modules.yaml`
- Create: `src/buddy_app/params/brain.yaml`
- Delete: `src/buddy_dialog/` (entire directory)
- Delete: `src/buddy_sentence/` (entire directory)
- Delete: `src/buddy_state_machine/` (entire directory)

- [ ] **Step 1: Update kComponents in buddy_main.cpp**

Replace lines 23-38 of `src/buddy_app/src/buddy_main.cpp`:

Old:
```cpp
static const std::vector<ComponentEntry> kComponents = {
    {"libaudio_component.so",
     "rclcpp_components::NodeFactoryTemplate<AudioPipelineNode>", "audio"},
    {"libvision_component.so",
     "rclcpp_components::NodeFactoryTemplate<VisionPipelineNode>", "vision"},
    {"libcloud_component.so",
     "rclcpp_components::NodeFactoryTemplate<CloudClientNode>", "cloud"},
    {"libstate_machine_component.so",
     "rclcpp_components::NodeFactoryTemplate<StateMachineNode>",
     "state_machine"},
    {"libdialog_component.so",
     "rclcpp_components::NodeFactoryTemplate<DialogManagerNode>", "dialog"},
    {"libsentence_component.so",
     "rclcpp_components::NodeFactoryTemplate<SentenceSegmenterNode>",
     "sentence"},
};
```

New:
```cpp
static const std::vector<ComponentEntry> kComponents = {
    {"libaudio_component.so",
     "rclcpp_components::NodeFactoryTemplate<AudioPipelineNode>", "audio"},
    {"libvision_component.so",
     "rclcpp_components::NodeFactoryTemplate<VisionPipelineNode>", "vision"},
    {"libbrain_component.so",
     "rclcpp_components::NodeFactoryTemplate<BrainNode>", "brain"},
    {"libcloud_component.so",
     "rclcpp_components::NodeFactoryTemplate<CloudClientNode>", "cloud"},
};
```

- [ ] **Step 2: Create brain.yaml**

```yaml
brain:
  ros__parameters:
    system_prompt_path: ""
    max_history_turns: 10
    emotion_trigger:
      enabled: true
      negative_emotions: ["sad", "angry", "fear"]
      confidence_threshold: 0.7
      duration_seconds: 3.0
      cooldown_seconds: 60.0
    voice_trigger:
      attach_image: true
```

Write to `src/buddy_app/params/brain.yaml`.

- [ ] **Step 3: Update modules.yaml**

Replace entire content of `src/buddy_app/params/modules.yaml`:

```yaml
# Toggle modules on/off for development.
# Set to false to skip loading a module at startup.
# Missing entries default to true (enabled).
modules:
  audio: false
  vision: true
  brain: false
  cloud: false
```

- [ ] **Step 4: Delete old packages**

```bash
rm -rf src/buddy_dialog src/buddy_sentence src/buddy_state_machine
```

- [ ] **Step 5: Update buddy_audio topic name**

The audio node subscribes to `/dialog/sentence` but brain now publishes to `/brain/sentence`. Update `src/buddy_audio/src/audio_pipeline_node.cpp` line 14-16:

Old:
```cpp
  sentence_sub_ = create_subscription<buddy_interfaces::msg::Sentence>(
      "/dialog/sentence", 10,
      std::bind(&AudioPipelineNode::on_sentence, this, std::placeholders::_1));
```

New:
```cpp
  sentence_sub_ = create_subscription<buddy_interfaces::msg::Sentence>(
      "/brain/sentence", 10,
      std::bind(&AudioPipelineNode::on_sentence, this, std::placeholders::_1));
```

- [ ] **Step 6: Update buddy_cloud subscription**

Cloud currently subscribes to `/dialog/user_input` (UserInput). It needs to subscribe to `/brain/cloud_request` (CloudRequest) instead. For now, make a minimal change to keep it building — full Doubao integration comes in Task 6.

Update `src/buddy_cloud/include/buddy_cloud/cloud_client_node.hpp`:

```cpp
#pragma once
#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/cloud_request.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class CloudClientNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit CloudClientNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void on_cloud_request(const buddy_interfaces::msg::CloudRequest &msg);
  rclcpp::Publisher<buddy_interfaces::msg::CloudChunk>::SharedPtr
      cloud_response_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::CloudRequest>::SharedPtr
      cloud_request_sub_;
};
```

Update `src/buddy_cloud/src/cloud_client_node.cpp`:

```cpp
#include "buddy_cloud/cloud_client_node.hpp"

CloudClientNode::CloudClientNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("cloud", options) {}

CallbackReturn CloudClientNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: configuring");
  cloud_response_pub_ = create_publisher<buddy_interfaces::msg::CloudChunk>(
      "/cloud/response", 10);
  cloud_request_sub_ =
      create_subscription<buddy_interfaces::msg::CloudRequest>(
          "/brain/cloud_request", 10,
          std::bind(&CloudClientNode::on_cloud_request, this,
                    std::placeholders::_1));
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: activating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: deactivating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: cleaning up");
  cloud_response_pub_.reset();
  cloud_request_sub_.reset();
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: shutting down");
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "CloudClientNode: error");
  return CallbackReturn::SUCCESS;
}

void CloudClientNode::on_cloud_request(
    const buddy_interfaces::msg::CloudRequest &msg) {
  RCLCPP_INFO(get_logger(), "Cloud request [%s]: %s",
              msg.trigger_type.c_str(), msg.user_text.c_str());
  // TODO: replace with Doubao API call (Task 6)
  auto chunk = buddy_interfaces::msg::CloudChunk();
  chunk.session_id = "stub";
  chunk.chunk_text = "Hello, I am Buddy!";
  chunk.is_final = true;
  cloud_response_pub_->publish(chunk);
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(CloudClientNode)
```

- [ ] **Step 7: Update cloud CMakeLists.txt to depend on sensor_msgs**

In `src/buddy_cloud/CMakeLists.txt`, add `find_package(sensor_msgs REQUIRED)` and add `sensor_msgs` to `ament_target_dependencies`. This is needed because CloudRequest.msg contains `sensor_msgs/Image`.

- [ ] **Step 8: Full build**

Run: `./build.sh`
Expected: Clean build with 4 business packages (audio, vision, brain, cloud) + interfaces + app.

- [ ] **Step 9: Run all tests**

Run: `colcon test --packages-select buddy_brain buddy_cloud buddy_audio buddy_vision && colcon test-result --verbose`
Expected: All tests pass.

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "feat(module): [PRO-10000] Replace dialog/sentence/state_machine with buddy_brain"
```

---

## Phase 1: Doubao Multimodal Cloud Integration

### Task 5: Update cloud.yaml with Doubao config

**Files:**
- Modify: `src/buddy_app/params/cloud.yaml`

- [ ] **Step 1: Replace cloud.yaml**

```yaml
cloud:
  ros__parameters:
    provider: "doubao"
    doubao:
      api_key: ""
      model: "doubao-1.5-pro"
      endpoint: "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
    image_max_width: 512
    timeout_seconds: 30
```

Write to `src/buddy_app/params/cloud.yaml`.

- [ ] **Step 2: Commit**

```bash
git add src/buddy_app/params/cloud.yaml
git commit -m "feat(module): [PRO-10000] Update cloud config for Doubao multimodal API"
```

---

### Task 6: Implement Doubao multimodal API in buddy_cloud

**Files:**
- Modify: `src/buddy_cloud/include/buddy_cloud/cloud_client_node.hpp`
- Modify: `src/buddy_cloud/src/cloud_client_node.cpp`
- Modify: `src/buddy_cloud/CMakeLists.txt`
- Modify: `src/buddy_cloud/package.xml`

- [ ] **Step 1: Add libcurl dependency to CMakeLists.txt**

In `src/buddy_cloud/CMakeLists.txt`, add after `find_package(buddy_interfaces REQUIRED)`:

```cmake
find_package(CURL REQUIRED)
```

And add to the target link:

```cmake
target_link_libraries(cloud_component ${CURL_LIBRARIES})
target_include_directories(cloud_component PRIVATE ${CURL_INCLUDE_DIRS})
```

- [ ] **Step 2: Add curl build depend to package.xml**

In `src/buddy_cloud/package.xml`, add:

```xml
<build_depend>libcurl4-openssl-dev</build_depend>
<exec_depend>libcurl4</exec_depend>
```

- [ ] **Step 3: Update cloud_client_node.hpp with Doubao fields**

```cpp
#pragma once
#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/cloud_request.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

#include <string>
#include <thread>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class CloudClientNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit CloudClientNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void on_cloud_request(const buddy_interfaces::msg::CloudRequest &msg);
  void call_doubao(const buddy_interfaces::msg::CloudRequest &msg);
  std::string encode_image_base64(const sensor_msgs::msg::Image &image,
                                  int max_width);

  rclcpp::Publisher<buddy_interfaces::msg::CloudChunk>::SharedPtr
      cloud_response_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::CloudRequest>::SharedPtr
      cloud_request_sub_;

  // Config
  std::string api_key_;
  std::string model_;
  std::string endpoint_;
  int image_max_width_{512};
  int timeout_seconds_{30};
};
```

- [ ] **Step 4: Implement Doubao HTTP call in cloud_client_node.cpp**

```cpp
#include "buddy_cloud/cloud_client_node.hpp"

#include <curl/curl.h>
#include <opencv2/opencv.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sstream>

// curl write callback
static size_t write_callback(char *ptr, size_t size, size_t nmemb,
                             void *userdata) {
  auto *response = static_cast<std::string *>(userdata);
  response->append(ptr, size * nmemb);
  return size * nmemb;
}

CloudClientNode::CloudClientNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("cloud", options) {}

CallbackReturn CloudClientNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: configuring");

  declare_parameter("provider", "doubao");
  declare_parameter("doubao.api_key", "");
  declare_parameter("doubao.model", "doubao-1.5-pro");
  declare_parameter("doubao.endpoint",
                    "https://ark.cn-beijing.volces.com/api/v3/chat/completions");
  declare_parameter("image_max_width", 512);
  declare_parameter("timeout_seconds", 30);

  api_key_ = get_parameter("doubao.api_key").as_string();
  model_ = get_parameter("doubao.model").as_string();
  endpoint_ = get_parameter("doubao.endpoint").as_string();
  image_max_width_ = get_parameter("image_max_width").as_int();
  timeout_seconds_ = get_parameter("timeout_seconds").as_int();

  // Override API key from env if set
  const char *env_key = std::getenv("DOUBAO_API_KEY");
  if (env_key && env_key[0] != '\0') {
    api_key_ = env_key;
  }

  cloud_response_pub_ = create_publisher<buddy_interfaces::msg::CloudChunk>(
      "/cloud/response", 10);
  cloud_request_sub_ =
      create_subscription<buddy_interfaces::msg::CloudRequest>(
          "/brain/cloud_request", 10,
          std::bind(&CloudClientNode::on_cloud_request, this,
                    std::placeholders::_1));

  curl_global_init(CURL_GLOBAL_DEFAULT);
  return CallbackReturn::SUCCESS;
}

CallbackReturn CloudClientNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: activating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: deactivating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: cleaning up");
  cloud_response_pub_.reset();
  cloud_request_sub_.reset();
  curl_global_cleanup();
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: shutting down");
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "CloudClientNode: error");
  return CallbackReturn::SUCCESS;
}

void CloudClientNode::on_cloud_request(
    const buddy_interfaces::msg::CloudRequest &msg) {
  RCLCPP_INFO(get_logger(), "Cloud request [%s]: %s",
              msg.trigger_type.c_str(), msg.user_text.c_str());
  // Run in thread to avoid blocking the executor
  std::thread([this, msg]() { call_doubao(msg); }).detach();
}

std::string
CloudClientNode::encode_image_base64(const sensor_msgs::msg::Image &image,
                                     int max_width) {
  // Convert ROS Image to cv::Mat (assuming bgr8 or rgb8 encoding)
  cv::Mat mat(image.height, image.width,
              image.encoding == "rgb8" ? CV_8UC3 : CV_8UC3, (void *)image.data.data());
  if (image.encoding == "rgb8") {
    cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
  }

  // Resize if needed
  if (mat.cols > max_width) {
    double scale = static_cast<double>(max_width) / mat.cols;
    cv::resize(mat, mat, cv::Size(), scale, scale);
  }

  // JPEG encode
  std::vector<uchar> buf;
  cv::imencode(".jpg", mat, buf);

  // Base64 encode
  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string b64;
  b64.reserve(((buf.size() + 2) / 3) * 4);
  for (size_t i = 0; i < buf.size(); i += 3) {
    uint32_t n = static_cast<uint32_t>(buf[i]) << 16;
    if (i + 1 < buf.size()) n |= static_cast<uint32_t>(buf[i + 1]) << 8;
    if (i + 2 < buf.size()) n |= static_cast<uint32_t>(buf[i + 2]);
    b64 += table[(n >> 18) & 0x3F];
    b64 += table[(n >> 12) & 0x3F];
    b64 += (i + 1 < buf.size()) ? table[(n >> 6) & 0x3F] : '=';
    b64 += (i + 2 < buf.size()) ? table[n & 0x3F] : '=';
  }
  return b64;
}

void CloudClientNode::call_doubao(
    const buddy_interfaces::msg::CloudRequest &msg) {
  if (api_key_.empty()) {
    RCLCPP_ERROR(get_logger(), "No API key configured for Doubao");
    auto chunk = buddy_interfaces::msg::CloudChunk();
    chunk.session_id = "error";
    chunk.chunk_text = "API key not configured.";
    chunk.is_final = true;
    cloud_response_pub_->publish(chunk);
    return;
  }

  // Build messages array for Doubao API
  std::ostringstream messages;
  messages << "[";

  // System prompt
  if (!msg.system_prompt.empty()) {
    messages << R"({"role":"system","content":")"
             << msg.system_prompt << R"("},)";
  }

  // Dialog history
  for (auto &h : msg.dialog_history) {
    // Format: "user: text" or "assistant: text"
    auto colon = h.find(": ");
    if (colon != std::string::npos) {
      auto role = h.substr(0, colon);
      auto content = h.substr(colon + 2);
      messages << R"({"role":")" << role << R"(","content":")" << content
               << R"("},)";
    }
  }

  // Current user message (multimodal content)
  messages << R"({"role":"user","content":[)";

  // Text part
  std::string text_content = msg.user_text;
  if (text_content.empty() && msg.trigger_type == "emotion") {
    text_content = "I notice you seem " + msg.emotion +
                   ". How are you feeling?";
  }
  if (!msg.emotion.empty()) {
    text_content += " [emotion: " + msg.emotion + " " +
                    std::to_string(static_cast<int>(msg.emotion_confidence * 100)) +
                    "%]";
  }
  messages << R"({"type":"text","text":")" << text_content << R"("})";

  // Image part (if present)
  if (!msg.image.data.empty()) {
    auto b64 = encode_image_base64(msg.image, image_max_width_);
    messages << R"(,{"type":"image_url","image_url":{"url":"data:image/jpeg;base64,)"
             << b64 << R"("}})";
  }

  messages << "]}]";

  // Build full request body
  std::ostringstream body;
  body << R"({"model":")" << model_
       << R"(","messages":)" << messages.str() << "}";

  // HTTP POST with libcurl
  CURL *curl = curl_easy_init();
  if (!curl) {
    RCLCPP_ERROR(get_logger(), "Failed to init curl");
    return;
  }

  std::string response;
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string auth = "Authorization: Bearer " + api_key_;
  headers = curl_slist_append(headers, auth.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.str().c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));

  auto res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  auto chunk = buddy_interfaces::msg::CloudChunk();
  chunk.session_id = "doubao";

  if (res != CURLE_OK) {
    RCLCPP_ERROR(get_logger(), "Doubao API error: %s",
                 curl_easy_strerror(res));
    chunk.chunk_text = "Cloud request failed.";
    chunk.is_final = true;
    cloud_response_pub_->publish(chunk);
    return;
  }

  // Extract content from response JSON (simple extraction)
  // Doubao response: {"choices":[{"message":{"content":"..."}}]}
  auto content_key = R"("content":")";
  auto pos = response.rfind(content_key);
  if (pos != std::string::npos) {
    pos += std::strlen(content_key);
    auto end = response.find('"', pos);
    chunk.chunk_text = response.substr(pos, end - pos);
  } else {
    RCLCPP_WARN(get_logger(), "Unexpected Doubao response: %s",
                response.c_str());
    chunk.chunk_text = response;
  }

  chunk.is_final = true;
  cloud_response_pub_->publish(chunk);
  RCLCPP_INFO(get_logger(), "Doubao response: %s", chunk.chunk_text.c_str());
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(CloudClientNode)
```

- [ ] **Step 5: Update cloud CMakeLists.txt fully**

Add OpenCV dependency too (for image encoding):

```cmake
find_package(OpenCV REQUIRED)
find_package(CURL REQUIRED)
```

And update target:

```cmake
ament_target_dependencies(
  cloud_component
  rclcpp
  rclcpp_lifecycle
  rclcpp_components
  std_msgs
  sensor_msgs
  buddy_interfaces
  OpenCV)
target_link_libraries(cloud_component ${CURL_LIBRARIES})
target_include_directories(cloud_component PRIVATE ${CURL_INCLUDE_DIRS})
```

- [ ] **Step 6: Build and verify**

Run: `./build.sh --packages-select buddy_interfaces buddy_cloud`
Expected: Clean build.

- [ ] **Step 7: Run cloud tests**

Run: `colcon test --packages-select buddy_cloud && colcon test-result --verbose`
Expected: Tests pass (lifecycle tests should still work).

- [ ] **Step 8: Commit**

```bash
git add src/buddy_cloud/ src/buddy_app/params/cloud.yaml
git commit -m "feat(module): [PRO-10000] Integrate Doubao multimodal API in buddy_cloud"
```

---

## Phase 2: Full Build Verification and Cleanup

### Task 7: Full system build and format

**Files:**
- All source files (formatting pass)

- [ ] **Step 1: Full build**

Run: `./build.sh`
Expected: Clean build of all packages.

- [ ] **Step 2: Run all tests**

Run: `colcon test && colcon test-result --verbose`
Expected: All tests pass.

- [ ] **Step 3: Format**

Run: `./format_and_lint.sh`

- [ ] **Step 4: Rebuild after formatting**

Run: `./build.sh`
Expected: Clean.

- [ ] **Step 5: Commit formatting changes if any**

```bash
git add -A
git commit -m "feat(module): [PRO-10000] Format all files after architecture simplification"
```

---

### Task 8: Update documentation

**Files:**
- Modify: `docs/architecture.md`
- Modify: `docs/communication_protocol.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update architecture.md**

In `docs/architecture.md`, update the "代码结构" section to reflect the new 4-package structure:

Replace:
```
- `src/buddy_state_machine`：流程编排
- `src/buddy_dialog`：对话管理
- `src/buddy_sentence`：切句
```

With:
```
- `src/buddy_brain`：中央编排（状态机 + 对话上下文 + 切句）
```

Also update the "运行拓扑" section to mention brain instead of state_machine.

- [ ] **Step 2: Update communication_protocol.md**

Add the new topics:
- `/brain/cloud_request` (CloudRequest) — brain → cloud
- `/brain/sentence` (Sentence) — brain → audio
- `/cloud/response` (CloudChunk) — cloud → brain

Remove old topics:
- `/dialog/user_input` — deleted with dialog
- `/dialog/cloud_response` — renamed to `/cloud/response`
- `/dialog/sentence` — renamed to `/brain/sentence`

- [ ] **Step 3: Update CLAUDE.md architecture section**

Update the module list and pipeline flow to reflect:
```
Audio → Brain → Cloud → Brain → Audio (TTS)
Vision → Brain (emotion context)
```

- [ ] **Step 4: Commit**

```bash
git add docs/ CLAUDE.md
git commit -m "feat(module): [PRO-10000] Update docs for 4-package architecture"
```

---

## Phase 3: ASR Replacement (Future — requires Sherpa-ONNX prebuilt)

> **Note:** This phase requires downloading and integrating the Sherpa-ONNX prebuilt library into `prebuilt/`. It is independent of Phases 0-2 and can be done in parallel once the library is available.

### Task 9: Integrate Sherpa-ONNX into buddy_audio

**Prerequisites:**
- Download Sherpa-ONNX C++ prebuilt library to `prebuilt/sherpa-onnx/`
- Download ASR model to `models/asr/`
- Download keyword spotting model to `models/keyword/`

**Files:**
- Modify: `src/buddy_audio/CMakeLists.txt`
- Modify: `src/buddy_audio/package.xml`
- Modify: `src/buddy_audio/include/buddy_audio/audio_pipeline_node.hpp`
- Modify: `src/buddy_audio/src/audio_pipeline_node.cpp`
- Create: `src/buddy_app/params/audio.yaml` (update with Sherpa-ONNX params)

This task is a placeholder with the architecture defined in the spec (Section 3). Implementation details depend on the specific Sherpa-ONNX version and model format chosen. The key changes:

- [ ] **Step 1: Add Sherpa-ONNX to CMakeLists (similar pattern to ONNX Runtime in buddy_vision)**

- [ ] **Step 2: Replace stub wake_word_pub_ with real keyword spotter thread**

- [ ] **Step 3: Replace stub asr_text_pub_ with streaming ASR thread**

- [ ] **Step 4: Keep TTS playback logic (on_sentence callback)**

- [ ] **Step 5: Build, test, commit**

---

## Summary: Execution Order

```
Task 1: CloudRequest message          (5 min)
Task 2: buddy_brain scaffold          (15 min)
Task 3: brain tests                   (10 min)
Task 4: Wire brain, delete old pkgs   (15 min)  ← system buildable again here
Task 5: Doubao cloud config           (2 min)
Task 6: Doubao API implementation     (20 min)
Task 7: Full build + format           (5 min)
Task 8: Update docs                   (10 min)
Task 9: Sherpa-ONNX ASR (future)      (TBD — needs prebuilt library)
```

Total estimated: ~1.5 hours for Tasks 1-8.
