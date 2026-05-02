# Dual-Brain Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add local Gemma 4 E2B inference (via ollama) alongside cloud Doubao inference, with local-first streaming and cloud replacement.

**Architecture:** New `buddy_local_llm` ROS 2 component runs parallel to `buddy_cloud`. Both subscribe to `/brain/request`. Brain receives responses on two separate topics (`/inference/local_chunk` and `/inference/cloud_chunk`), plays local response first, interrupts and replaces with cloud response when it arrives. Message types are renamed from `CloudRequest`/`CloudChunk` to `InferenceRequest`/`InferenceChunk`.

**Tech Stack:** C++17, ROS 2 Humble (rclcpp_components, lifecycle nodes), libcurl, ollama HTTP API, ament_cmake_gtest

---

## File Structure

### New Files
- `src/buddy_interfaces/msg/InferenceRequest.msg` — renamed from CloudRequest.msg (identical fields)
- `src/buddy_interfaces/msg/InferenceChunk.msg` — renamed from CloudChunk.msg (identical fields)
- `src/buddy_local_llm/CMakeLists.txt` — build config
- `src/buddy_local_llm/package.xml` — package manifest
- `src/buddy_local_llm/include/buddy_local_llm/local_llm_node.hpp` — component header
- `src/buddy_local_llm/src/local_llm_node.cpp` — component implementation
- `src/buddy_local_llm/src/ollama_client.cpp` — ollama HTTP client
- `src/buddy_local_llm/include/buddy_local_llm/ollama_client.hpp` — client header
- `src/buddy_local_llm/test/test_local_llm_node.cpp` — tests
- `src/buddy_app/params/local_llm.yaml` — config

### Modified Files
- `src/buddy_interfaces/CMakeLists.txt` — register new .msg files, remove old
- `src/buddy_interfaces/msg/CloudRequest.msg` — **delete**
- `src/buddy_interfaces/msg/CloudChunk.msg` — **delete**
- `src/buddy_cloud/include/buddy_cloud/cloud_client_node.hpp` — rename message types + topics
- `src/buddy_cloud/src/cloud_client_node.cpp` — rename message types + topics
- `src/buddy_brain/include/buddy_brain/brain_node.hpp` — add dual subscription + replacement logic
- `src/buddy_brain/src/brain_node.cpp` — add dual subscription + replacement logic
- `src/buddy_app/src/buddy_main.cpp` — add buddy_local_llm to component list
- `src/buddy_app/params/modules.yaml` — add local_llm entry
- `docs/architecture.md` — update topology
- `docs/communication_protocol.md` — update topic/message names
- `docs/plan.md` — add dual-brain phase

---

### Task 1: Rename messages in buddy_interfaces

**Files:**
- Create: `src/buddy_interfaces/msg/InferenceRequest.msg`
- Create: `src/buddy_interfaces/msg/InferenceChunk.msg`
- Delete: `src/buddy_interfaces/msg/CloudRequest.msg`
- Delete: `src/buddy_interfaces/msg/CloudChunk.msg`
- Modify: `src/buddy_interfaces/CMakeLists.txt`

- [ ] **Step 1: Create InferenceRequest.msg (identical to CloudRequest.msg)**

File: `src/buddy_interfaces/msg/InferenceRequest.msg`
```
# InferenceRequest.msg — brain → inference backends
string trigger_type           # "voice" or "emotion"
string user_text              # ASR text (voice trigger)
string emotion                # current emotion label
float32 emotion_confidence    # emotion confidence
string[] dialog_history       # recent N turns
string system_prompt          # system prompt text
sensor_msgs/Image image       # camera snapshot (optional)
```

- [ ] **Step 2: Create InferenceChunk.msg (identical to CloudChunk.msg)**

File: `src/buddy_interfaces/msg/InferenceChunk.msg`
```
string session_id
string chunk_text
bool is_final
```

- [ ] **Step 3: Delete old message files**

```bash
rm src/buddy_interfaces/msg/CloudRequest.msg src/buddy_interfaces/msg/CloudChunk.msg
```

- [ ] **Step 4: Update CMakeLists.txt to register new messages**

In `src/buddy_interfaces/CMakeLists.txt`, replace lines 10-20:

```cmake
rosidl_generate_interfaces(
  ${PROJECT_NAME}
  "msg/InferenceChunk.msg"
  "msg/Sentence.msg"
  "msg/EmotionResult.msg"
  "msg/InferenceRequest.msg"
  "srv/CaptureImage.srv"
  DEPENDENCIES
  builtin_interfaces
  sensor_msgs
  std_msgs)
```

- [ ] **Step 5: Build buddy_interfaces and verify**

Run: `./build.sh --packages-select buddy_interfaces`
Expected: Build succeeds with no errors.

- [ ] **Step 6: Commit**

```bash
git add -A src/buddy_interfaces/
git commit -m "feat(module): [PRO-10000] Rename CloudRequest/CloudChunk to InferenceRequest/InferenceChunk"
```

---

### Task 2: Update buddy_cloud to use new message types and topics

**Files:**
- Modify: `src/buddy_cloud/include/buddy_cloud/cloud_client_node.hpp`
- Modify: `src/buddy_cloud/src/cloud_client_node.cpp`

- [ ] **Step 1: Update header includes and member types**

In `src/buddy_cloud/include/buddy_cloud/cloud_client_node.hpp`, change:

Old (lines 2-3):
```cpp
#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/cloud_request.hpp>
```

New:
```cpp
#include <buddy_interfaces/msg/inference_chunk.hpp>
#include <buddy_interfaces/msg/inference_request.hpp>
```

Old (line 26):
```cpp
void on_cloud_request(const buddy_interfaces::msg::CloudRequest &msg);
```

New:
```cpp
void on_inference_request(const buddy_interfaces::msg::InferenceRequest &msg);
```

Old (line 27):
```cpp
void call_doubao(const buddy_interfaces::msg::CloudRequest &msg);
```

New:
```cpp
void call_doubao(const buddy_interfaces::msg::InferenceRequest &msg);
```

Old (lines 31-34):
```cpp
rclcpp::Publisher<buddy_interfaces::msg::CloudChunk>::SharedPtr
    cloud_response_pub_;
rclcpp::Subscription<buddy_interfaces::msg::CloudRequest>::SharedPtr
    cloud_request_sub_;
```

New:
```cpp
rclcpp::Publisher<buddy_interfaces::msg::InferenceChunk>::SharedPtr
    cloud_chunk_pub_;
rclcpp::Subscription<buddy_interfaces::msg::InferenceRequest>::SharedPtr
    inference_request_sub_;
```

- [ ] **Step 2: Update implementation — publisher/subscriber creation**

In `src/buddy_cloud/src/cloud_client_node.cpp`, change `on_configure`:

Old (lines 84-89):
```cpp
cloud_response_pub_ = create_publisher<buddy_interfaces::msg::CloudChunk>(
    "/cloud/response", 10);
cloud_request_sub_ = create_subscription<buddy_interfaces::msg::CloudRequest>(
    "/brain/cloud_request", 10,
    std::bind(&CloudClientNode::on_cloud_request, this,
              std::placeholders::_1));
```

New:
```cpp
cloud_chunk_pub_ = create_publisher<buddy_interfaces::msg::InferenceChunk>(
    "/inference/cloud_chunk", 10);
inference_request_sub_ =
    create_subscription<buddy_interfaces::msg::InferenceRequest>(
        "/brain/request", 10,
        std::bind(&CloudClientNode::on_inference_request, this,
                  std::placeholders::_1));
```

Old (lines 110-112) in `on_cleanup`:
```cpp
cloud_response_pub_.reset();
cloud_request_sub_.reset();
```

New:
```cpp
cloud_chunk_pub_.reset();
inference_request_sub_.reset();
```

- [ ] **Step 3: Update implementation — callback and call_doubao signatures**

Rename `on_cloud_request` to `on_inference_request`:

Old (lines 125-135):
```cpp
void CloudClientNode::on_cloud_request(
    const buddy_interfaces::msg::CloudRequest &msg) {
  RCLCPP_INFO(get_logger(), "Cloud request [%s]: %s", msg.trigger_type.c_str(),
              msg.user_text.c_str());
  std::lock_guard<std::mutex> lock(request_mtx_);
  if (worker_thread_.joinable()) {
    RCLCPP_WARN(get_logger(), "Previous request still in progress, waiting");
    worker_thread_.join();
  }
  worker_thread_ = std::thread([this, msg]() { call_doubao(msg); });
}
```

New:
```cpp
void CloudClientNode::on_inference_request(
    const buddy_interfaces::msg::InferenceRequest &msg) {
  RCLCPP_INFO(get_logger(), "Inference request [%s]: %s",
              msg.trigger_type.c_str(), msg.user_text.c_str());
  std::lock_guard<std::mutex> lock(request_mtx_);
  if (worker_thread_.joinable()) {
    RCLCPP_WARN(get_logger(), "Previous request still in progress, waiting");
    worker_thread_.join();
  }
  worker_thread_ = std::thread([this, msg]() { call_doubao(msg); });
}
```

Rename `call_doubao` signature:

Old (line 171):
```cpp
void CloudClientNode::call_doubao(
    const buddy_interfaces::msg::CloudRequest &msg) {
```

New:
```cpp
void CloudClientNode::call_doubao(
    const buddy_interfaces::msg::InferenceRequest &msg) {
```

In `call_doubao`, change all `buddy_interfaces::msg::CloudChunk` to `buddy_interfaces::msg::InferenceChunk` and `cloud_response_pub_` to `cloud_chunk_pub_`:

Line 175:
```cpp
auto chunk = buddy_interfaces::msg::InferenceChunk();
```

Line 179:
```cpp
cloud_chunk_pub_->publish(chunk);
```

Line 254:
```cpp
auto chunk = buddy_interfaces::msg::InferenceChunk();
```

Line 261:
```cpp
cloud_chunk_pub_->publish(chunk);
```

Line 278:
```cpp
cloud_chunk_pub_->publish(chunk);
```

- [ ] **Step 4: Build buddy_cloud and verify**

Run: `./build.sh --packages-select buddy_interfaces buddy_cloud`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/buddy_cloud/
git commit -m "feat(module): [PRO-10000] Update buddy_cloud to use InferenceRequest/InferenceChunk"
```

---

### Task 3: Create buddy_local_llm package skeleton

**Files:**
- Create: `src/buddy_local_llm/package.xml`
- Create: `src/buddy_local_llm/CMakeLists.txt`
- Create: `src/buddy_local_llm/include/buddy_local_llm/ollama_client.hpp`
- Create: `src/buddy_local_llm/include/buddy_local_llm/local_llm_node.hpp`
- Create: `src/buddy_local_llm/src/ollama_client.cpp`
- Create: `src/buddy_local_llm/src/local_llm_node.cpp`
- Create: `src/buddy_local_llm/test/test_local_llm_node.cpp`

- [ ] **Step 1: Create package.xml**

File: `src/buddy_local_llm/package.xml`
```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>buddy_local_llm</name>
  <version>0.1.0</version>
  <description>Local LLM inference via ollama for fast initial responses</description>
  <maintainer email="dev@buddy.robot">seb</maintainer>
  <license>Apache-2.0</license>
  <buildtool_depend>ament_cmake</buildtool_depend>
  <depend>rclcpp</depend>
  <depend>rclcpp_lifecycle</depend>
  <depend>rclcpp_components</depend>
  <depend>buddy_interfaces</depend>
  <test_depend>ament_cmake_gtest</test_depend>
  <build_depend>libcurl4-openssl-dev</build_depend>
  <exec_depend>libcurl4</exec_depend>
  <export><build_type>ament_cmake</build_type></export>
</package>
```

- [ ] **Step 2: Create CMakeLists.txt**

File: `src/buddy_local_llm/CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_local_llm)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(buddy_interfaces REQUIRED)
find_package(CURL REQUIRED)

add_library(local_llm_component SHARED
  src/local_llm_node.cpp
  src/ollama_client.cpp)
target_include_directories(
  local_llm_component PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                             $<INSTALL_INTERFACE:include>)
target_compile_features(local_llm_component PUBLIC cxx_std_17)
ament_target_dependencies(
  local_llm_component
  rclcpp
  rclcpp_lifecycle
  rclcpp_components
  buddy_interfaces)
rclcpp_components_register_nodes(local_llm_component "LocalLlmNode")
target_link_libraries(local_llm_component ${CURL_LIBRARIES})
target_include_directories(local_llm_component PRIVATE ${CURL_INCLUDE_DIRS})
install(
  TARGETS local_llm_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_local_llm_node test/test_local_llm_node.cpp
                  src/local_llm_node.cpp src/ollama_client.cpp)
  target_include_directories(
    test_local_llm_node
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_local_llm_node PUBLIC cxx_std_17)
  ament_target_dependencies(
    test_local_llm_node
    rclcpp
    rclcpp_lifecycle
    rclcpp_components
    buddy_interfaces)
  target_link_libraries(test_local_llm_node ${CURL_LIBRARIES})
  target_include_directories(test_local_llm_node PRIVATE ${CURL_INCLUDE_DIRS})
endif()
ament_package()
```

- [ ] **Step 3: Create ollama_client.hpp**

File: `src/buddy_local_llm/include/buddy_local_llm/ollama_client.hpp`
```cpp
#pragma once
#include <functional>
#include <string>
#include <vector>

struct ChatMessage {
  std::string role;
  std::string content;
};

class OllamaClient {
public:
  // Called for each streamed text chunk. done=true means stream ended.
  using ChunkCallback =
      std::function<void(const std::string &chunk, bool done)>;

  OllamaClient(const std::string &api_url, const std::string &model,
               int timeout_seconds);

  // Returns true on success, false on error.
  bool chat_streaming(const std::vector<ChatMessage> &messages,
                      const ChunkCallback &callback);

private:
  std::string api_url_;
  std::string model_;
  int timeout_seconds_;
};
```

- [ ] **Step 4: Create ollama_client.cpp**

File: `src/buddy_local_llm/src/ollama_client.cpp`
```cpp
#include "buddy_local_llm/ollama_client.hpp"

#include <curl/curl.h>
#include <sstream>

static std::string json_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size() * 2);
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  return out;
}

struct StreamContext {
  std::string buffer;
  OllamaClient::ChunkCallback callback;
};

static size_t stream_callback(char *ptr, size_t size, size_t nmemb,
                              void *userdata) {
  auto *ctx = static_cast<StreamContext *>(userdata);
  ctx->buffer.append(ptr, size * nmemb);

  // ollama returns one JSON object per line
  auto &buf = ctx->buffer;
  size_t pos = 0;
  while (pos < buf.size()) {
    auto nl = buf.find('\n', pos);
    if (nl == std::string::npos)
      break;

    std::string line = buf.substr(pos, nl - pos);
    pos = nl + 1;

    if (line.empty())
      continue;

    // Extract "message":{"content":"..."} and "done":true/false
    auto content_key = std::string(R"("content":")");
    auto cp = line.find(content_key);
    if (cp != std::string::npos) {
      cp += content_key.size();
      auto end = line.find('"', cp);
      if (end != std::string::npos) {
        auto text = line.substr(cp, end - cp);
        // Unescape \n
        std::string unescaped;
        for (size_t i = 0; i < text.size(); ++i) {
          if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
            unescaped += '\n';
            ++i;
          } else {
            unescaped += text[i];
          }
        }
        ctx->callback(unescaped, false);
      }
    }

    auto done_key = std::string(R"("done":true)");
    if (line.find(done_key) != std::string::npos) {
      ctx->callback("", true);
    }
  }

  buf = buf.substr(pos);
  return size * nmemb;
}

OllamaClient::OllamaClient(const std::string &api_url,
                           const std::string &model, int timeout_seconds)
    : api_url_(api_url), model_(model), timeout_seconds_(timeout_seconds) {}

bool OllamaClient::chat_streaming(const std::vector<ChatMessage> &messages,
                                  const ChunkCallback &callback) {
  std::ostringstream body;
  body << R"({"model":")" << json_escape(model_) << R"(","messages":[)";

  for (size_t i = 0; i < messages.size(); ++i) {
    if (i > 0)
      body << ",";
    body << R"({"role":")" << json_escape(messages[i].role) << R"(","content":")"
         << json_escape(messages[i].content) << R"("})";
  }

  body << R"(],"stream":true})";

  std::string body_str = body.str();

  CURL *curl = curl_easy_init();
  if (!curl)
    return false;

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  StreamContext ctx;
  ctx.callback = callback;

  curl_easy_setopt(curl, CURLOPT_URL, (api_url_ + "/api/chat").c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT,
                   static_cast<long>(timeout_seconds_));

  auto res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return res == CURLE_OK;
}
```

- [ ] **Step 5: Create local_llm_node.hpp**

File: `src/buddy_local_llm/include/buddy_local_llm/local_llm_node.hpp`
```cpp
#pragma once
#include <buddy_interfaces/msg/inference_chunk.hpp>
#include <buddy_interfaces/msg/inference_request.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

#include "buddy_local_llm/ollama_client.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <thread>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class LocalLlmNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit LocalLlmNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void on_inference_request(
      const buddy_interfaces::msg::InferenceRequest &msg);
  void handle_request(const buddy_interfaces::msg::InferenceRequest &msg);

  rclcpp::Publisher<buddy_interfaces::msg::InferenceChunk>::SharedPtr
      local_chunk_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::InferenceRequest>::SharedPtr
      inference_request_sub_;

  std::unique_ptr<OllamaClient> client_;
  std::string model_name_;
  std::string system_prompt_;

  std::thread worker_thread_;
  std::mutex request_mtx_;
};
```

- [ ] **Step 6: Create local_llm_node.cpp**

File: `src/buddy_local_llm/src/local_llm_node.cpp`
```cpp
#include "buddy_local_llm/local_llm_node.hpp"

#include <curl/curl.h>

LocalLlmNode::LocalLlmNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("local_llm", options) {}

CallbackReturn LocalLlmNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "LocalLlmNode: configuring");

  declare_parameter("model_name", "gemma4:e2b");
  declare_parameter("api_url", "http://localhost:11434");
  declare_parameter("timeout_seconds", 5);
  declare_parameter("system_prompt",
                    "你是一个友好的机器人助手，请用简短自然的方式回复");

  model_name_ = get_parameter("model_name").as_string();
  auto api_url = get_parameter("api_url").as_string();
  auto timeout = get_parameter("timeout_seconds").as_int();
  system_prompt_ = get_parameter("system_prompt").as_string();

  client_ = std::make_unique<OllamaClient>(api_url, model_name_, timeout);

  local_chunk_pub_ = create_publisher<buddy_interfaces::msg::InferenceChunk>(
      "/inference/local_chunk", 10);
  inference_request_sub_ =
      create_subscription<buddy_interfaces::msg::InferenceRequest>(
          "/brain/request", 10,
          std::bind(&LocalLlmNode::on_inference_request, this,
                    std::placeholders::_1));

  curl_global_init(CURL_GLOBAL_DEFAULT);
  return CallbackReturn::SUCCESS;
}

CallbackReturn LocalLlmNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "LocalLlmNode: activating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn LocalLlmNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "LocalLlmNode: deactivating");
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
  return CallbackReturn::SUCCESS;
}
CallbackReturn LocalLlmNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "LocalLlmNode: cleaning up");
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
  local_chunk_pub_.reset();
  inference_request_sub_.reset();
  client_.reset();
  curl_global_cleanup();
  return CallbackReturn::SUCCESS;
}
CallbackReturn LocalLlmNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "LocalLlmNode: shutting down");
  return CallbackReturn::SUCCESS;
}
CallbackReturn LocalLlmNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "LocalLlmNode: error");
  return CallbackReturn::SUCCESS;
}

void LocalLlmNode::on_inference_request(
    const buddy_interfaces::msg::InferenceRequest &msg) {
  RCLCPP_INFO(get_logger(), "Local request: %s", msg.user_text.c_str());
  std::lock_guard<std::mutex> lock(request_mtx_);
  if (worker_thread_.joinable()) {
    RCLCPP_WARN(get_logger(),
                "Previous local request still in progress, waiting");
    worker_thread_.join();
  }
  worker_thread_ = std::thread([this, msg]() { handle_request(msg); });
}

void LocalLlmNode::handle_request(
    const buddy_interfaces::msg::InferenceRequest &msg) {
  std::vector<ChatMessage> messages;
  if (!system_prompt_.empty()) {
    messages.push_back({"system", system_prompt_});
  }

  std::string user_text = msg.user_text;
  if (user_text.empty()) {
    user_text = "你好";
  }
  messages.push_back({"user", user_text});

  bool ok = client_->chat_streaming(
      messages,
      [this](const std::string &chunk, bool done) {
        auto msg = buddy_interfaces::msg::InferenceChunk();
        msg.session_id = "local";
        msg.chunk_text = chunk;
        msg.is_final = done;
        local_chunk_pub_->publish(msg);
      });

  if (!ok) {
    auto err = buddy_interfaces::msg::InferenceChunk();
    err.session_id = "local";
    err.chunk_text = "";
    err.is_final = true;
    local_chunk_pub_->publish(err);
    RCLCPP_WARN(get_logger(), "Local LLM request failed");
  }
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(LocalLlmNode)
```

- [ ] **Step 7: Create test file**

File: `src/buddy_local_llm/test/test_local_llm_node.cpp`
```cpp
#include "buddy_local_llm/local_llm_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class LocalLlmNodeTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<LocalLlmNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<LocalLlmNode> node_;
};

TEST_F(LocalLlmNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), std::string("local_llm"));
}

TEST_F(LocalLlmNodeTest, ConfigureTransition) {
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
}

TEST_F(LocalLlmNodeTest, FullLifecycleSequence) {
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

- [ ] **Step 8: Build buddy_local_llm and verify**

Run: `./build.sh --packages-select buddy_interfaces buddy_local_llm`
Expected: Build succeeds.

- [ ] **Step 9: Run tests**

Run: `./output/build/buddy_local_llm/test_local_llm_node`
Expected: All 3 tests PASS.

- [ ] **Step 10: Commit**

```bash
git add src/buddy_local_llm/
git commit -m "feat(module): [PRO-10000] Add buddy_local_llm package with ollama client"
```

---

### Task 4: Update buddy_brain for dual-stream subscription and replacement logic

**Files:**
- Modify: `src/buddy_brain/include/buddy_brain/brain_node.hpp`
- Modify: `src/buddy_brain/src/brain_node.cpp`

- [ ] **Step 1: Update header includes and member declarations**

In `src/buddy_brain/include/buddy_brain/brain_node.hpp`, change:

Old (lines 2-3):
```cpp
#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/cloud_request.hpp>
```

New:
```cpp
#include <buddy_interfaces/msg/inference_chunk.hpp>
#include <buddy_interfaces/msg/inference_request.hpp>
```

Old (lines 42-43):
```cpp
void on_cloud_chunk(const buddy_interfaces::msg::CloudChunk &msg);
```

New:
```cpp
void on_local_chunk(const buddy_interfaces::msg::InferenceChunk &msg);
void on_cloud_chunk(const buddy_interfaces::msg::InferenceChunk &msg);
```

Old (line 46):
```cpp
void request_cloud(const std::string &trigger_type,
                   const std::string &user_text);
```

New:
```cpp
void request_inference(const std::string &trigger_type,
                       const std::string &user_text);
```

After `sentence_index_{0};` (line 72), add:
```cpp
bool first_cloud_chunk_{true};
```

Old (lines 74-75):
```cpp
rclcpp::Publisher<buddy_interfaces::msg::CloudRequest>::SharedPtr
    cloud_request_pub_;
```

New:
```cpp
rclcpp::Publisher<buddy_interfaces::msg::InferenceRequest>::SharedPtr
    inference_request_pub_;
```

Old (lines 82-83):
```cpp
rclcpp::Subscription<buddy_interfaces::msg::CloudChunk>::SharedPtr
    cloud_chunk_sub_;
```

New:
```cpp
rclcpp::Subscription<buddy_interfaces::msg::InferenceChunk>::SharedPtr
    local_chunk_sub_;
rclcpp::Subscription<buddy_interfaces::msg::InferenceChunk>::SharedPtr
    cloud_chunk_sub_;
```

- [ ] **Step 2: Update on_configure — publishers and subscribers**

In `src/buddy_brain/src/brain_node.cpp`, change:

Old (lines 50-51):
```cpp
cloud_request_pub_ = create_publisher<buddy_interfaces::msg::CloudRequest>(
    "/brain/cloud_request", 10);
```

New:
```cpp
inference_request_pub_ =
    create_publisher<buddy_interfaces::msg::InferenceRequest>("/brain/request",
                                                              10);
```

Old (lines 64-66):
```cpp
cloud_chunk_sub_ = create_subscription<buddy_interfaces::msg::CloudChunk>(
    "/cloud/response", 10,
    std::bind(&BrainNode::on_cloud_chunk, this, std::placeholders::_1));
```

New:
```cpp
local_chunk_sub_ = create_subscription<buddy_interfaces::msg::InferenceChunk>(
    "/inference/local_chunk", 10,
    std::bind(&BrainNode::on_local_chunk, this, std::placeholders::_1));
cloud_chunk_sub_ = create_subscription<buddy_interfaces::msg::InferenceChunk>(
    "/inference/cloud_chunk", 10,
    std::bind(&BrainNode::on_cloud_chunk, this, std::placeholders::_1));
```

In `on_cleanup`, old (lines 89-90):
```cpp
cloud_request_pub_.reset();
```

New:
```cpp
inference_request_pub_.reset();
```

Old (line 94):
```cpp
cloud_chunk_sub_.reset();
```

New:
```cpp
local_chunk_sub_.reset();
cloud_chunk_sub_.reset();
```

- [ ] **Step 3: Implement on_local_chunk callback**

In `src/buddy_brain/src/brain_node.cpp`, before `on_cloud_chunk`, add:

```cpp
void BrainNode::on_local_chunk(
    const buddy_interfaces::msg::InferenceChunk &msg) {
  if (state_ != State::REQUESTING)
    return;

  if (!msg.chunk_text.empty()) {
    auto sentences = segment(msg.chunk_text);
    for (auto &s : sentences) {
      auto sentence_msg = buddy_interfaces::msg::Sentence();
      sentence_msg.session_id = session_id_;
      sentence_msg.text = s;
      sentence_msg.index = sentence_index_++;
      sentence_pub_->publish(sentence_msg);
    }
  }

  if (msg.is_final) {
    flush_sentence_buffer(session_id_);
  }
}
```

- [ ] **Step 4: Update on_cloud_chunk with replacement logic**

Replace the entire `on_cloud_chunk` method (lines 163-187):

```cpp
void BrainNode::on_cloud_chunk(
    const buddy_interfaces::msg::InferenceChunk &msg) {
  if (state_ != State::REQUESTING)
    return;

  if (first_cloud_chunk_) {
    first_cloud_chunk_ = false;
    // TODO: interrupt local TTS playback (requires audio support)
    sentence_buffer_.clear();
    sentence_index_ = 0;
    RCLCPP_INFO(get_logger(), "Cloud response arrived, replacing local");
  }

  if (!msg.chunk_text.empty()) {
    auto sentences = segment(msg.chunk_text);
    for (auto &s : sentences) {
      auto sentence_msg = buddy_interfaces::msg::Sentence();
      sentence_msg.session_id = session_id_;
      sentence_msg.text = s;
      sentence_msg.index = sentence_index_++;
      sentence_pub_->publish(sentence_msg);
    }
  }

  if (msg.is_final) {
    flush_sentence_buffer(session_id_);
    if (!msg.chunk_text.empty()) {
      history_.push_back("assistant: " + msg.chunk_text);
      trim_history();
    }
    transition(State::SPEAKING);
  }
}
```

- [ ] **Step 5: Rename request_cloud to request_inference**

In `brain_node.cpp`, rename the function `request_cloud` to `request_inference` and update the message type:

Old (line 204):
```cpp
void BrainNode::request_cloud(const std::string &trigger_type,
                              const std::string &user_text) {
```

New:
```cpp
void BrainNode::request_inference(const std::string &trigger_type,
                                  const std::string &user_text) {
```

Old (line 213):
```cpp
auto req = buddy_interfaces::msg::CloudRequest();
```

New:
```cpp
auto req = buddy_interfaces::msg::InferenceRequest();
```

Old (line 239):
```cpp
cloud_request_pub_->publish(req);
```

New:
```cpp
inference_request_pub_->publish(req);
```

After `sentence_index_ = 0;` (line 211), add:
```cpp
first_cloud_chunk_ = true;
```

- [ ] **Step 6: Update callers of request_cloud**

In `on_asr_text` (line 124):
```cpp
request_inference(trigger_types::kVoice, msg.data);
```

In `on_emotion` (line 156):
```cpp
request_inference(trigger_types::kEmotion, "");
```

- [ ] **Step 7: Build buddy_brain and verify**

Run: `./build.sh --packages-select buddy_interfaces buddy_brain`
Expected: Build succeeds.

- [ ] **Step 8: Run brain tests**

Run: `./output/build/buddy_brain/test_brain_node && ./output/build/buddy_brain/test_segment`
Expected: All tests PASS.

- [ ] **Step 9: Commit**

```bash
git add src/buddy_brain/
git commit -m "feat(module): [PRO-10000] Update buddy_brain for dual-stream local/cloud inference"
```

---

### Task 5: Update buddy_app to load buddy_local_llm

**Files:**
- Modify: `src/buddy_app/src/buddy_main.cpp`
- Create: `src/buddy_app/params/local_llm.yaml`
- Modify: `src/buddy_app/params/modules.yaml`

- [ ] **Step 1: Add component entry in buddy_main.cpp**

In `src/buddy_app/src/buddy_main.cpp`, insert before `libbrain_component.so` line (between cloud and brain, matching spec load order):

Old (lines 23-32):
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

New:
```cpp
static const std::vector<ComponentEntry> kComponents = {
    {"libaudio_component.so",
     "rclcpp_components::NodeFactoryTemplate<AudioPipelineNode>", "audio"},
    {"libvision_component.so",
     "rclcpp_components::NodeFactoryTemplate<VisionPipelineNode>", "vision"},
    {"libcloud_component.so",
     "rclcpp_components::NodeFactoryTemplate<CloudClientNode>", "cloud"},
    {"liblocal_llm_component.so",
     "rclcpp_components::NodeFactoryTemplate<LocalLlmNode>", "local_llm"},
    {"libbrain_component.so",
     "rclcpp_components::NodeFactoryTemplate<BrainNode>", "brain"},
};
```

- [ ] **Step 2: Create local_llm.yaml config**

File: `src/buddy_app/params/local_llm.yaml`
```yaml
local_llm:
  ros__parameters:
    model_name: "gemma4:e2b"
    api_url: "http://localhost:11434"
    timeout_seconds: 5
    system_prompt: "你是一个友好的机器人助手，请用简短自然的方式回复"
```

- [ ] **Step 3: Update modules.yaml**

In `src/buddy_app/params/modules.yaml`, add `local_llm: false` entry:

```yaml
modules:
  audio: false
  vision: true
  brain: false
  cloud: false
  local_llm: false
```

- [ ] **Step 4: Build all packages and verify**

Run: `./build.sh`
Expected: Full build succeeds.

- [ ] **Step 5: Run all tests**

Run: `./output/build/buddy_brain/test_brain_node && ./output/build/buddy_brain/test_segment && ./output/build/buddy_cloud/test_cloud_node && ./output/build/buddy_local_llm/test_local_llm_node`
Expected: All tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/buddy_app/ buddy_local_llm/
git commit -m "feat(module): [PRO-10000] Add buddy_local_llm to app loader and config"
```

---

### Task 6: Update documentation

**Files:**
- Modify: `docs/architecture.md`
- Modify: `docs/communication_protocol.md`
- Modify: `docs/plan.md`

- [ ] **Step 1: Update architecture.md**

Update section 2 (Code Structure) to add buddy_local_llm and section 3 (Running Topology) to reflect dual-brain flow.

In `docs/architecture.md`, update to:

```markdown
# Buddy Robot 架构文档（ROS 2 组件化）

版本: v6.0
日期: 2026-05-03
状态: 当前有效

## 1. 架构结论

项目采用 ROS 2 组件化单容器部署：

1. 所有业务模块以 `rclcpp_components` 方式实现。
2. 通过 `buddy_app/src/buddy_main.cpp` 使用 class_loader 加载所有组件到单进程。
3. 模块间通信使用 ROS 2 Topic/Service，自定义协议在 `buddy_interfaces` 中定义。
4. 参数配置放在 `buddy_app/params/*.yaml`。

## 2. 代码结构

- `src/buddy_interfaces`：消息与服务定义
- `src/buddy_audio`：音频入口与 TTS 回执
- `src/buddy_vision`：视觉处理链路（详见 [vision_architecture.md](vision_architecture.md)）
- `src/buddy_cloud`：云端推理请求（Doubao API）
- `src/buddy_local_llm`：本地推理请求（ollama + Gemma 4 E2B）
- `src/buddy_brain`：中央编排（状态机 + 对话上下文 + 切句 + 双流合并）
- `src/buddy_app`：C++ 入口程序，加载所有组件，包含参数配置

## 3. 运行拓扑

启动后组件运行在同一个进程内（`buddy_main`），通过 class_loader 动态加载组件 .so，开启 intra-process 通信。

主流程（双脑架构）：

```
Audio → Brain → Vision (optional) → Local LLM + Cloud → Brain → Audio playback
                                                  ↑
                                          双模型并行推理
                                    本地先回复，云端替换
```

1. buddy_audio — 唤醒词检测、ASR、TTS回放
2. buddy_brain — 状态机、对话上下文、切句、双流合并
3. buddy_vision — 图像采集与情感识别
4. buddy_cloud — 豆包API多模态请求（云端大模型）
5. buddy_local_llm — ollama本地推理（快速初始回复）
6. buddy_brain — 本地回复先播，云端到了替换 → audio TTS

## 4. 依赖策略

1. 本仓库不再使用 Conan。
2. ROS2 基础依赖来自预编译 tarball，解压到 `prebuilt/` 后由 `build.sh` 自动 source。
3. 第三方模型文件（`.onnx`、`.rknn`）不纳入版本库。
4. 本地 LLM 推理依赖外部 ollama 服务（默认 `localhost:11434`）。
```

- [ ] **Step 2: Update communication_protocol.md**

Update topic table and message list in `docs/communication_protocol.md`:

```markdown
# Buddy Robot 通信协议（ROS 2）

版本: v3.0
日期: 2026-05-03
状态: 当前有效

## 1. 范围

本规范定义仓库内 ROS 2 模块之间的通信约定。

## 2. 消息与服务定义

自定义接口位于：`src/buddy_interfaces`

- 消息：`InferenceRequest.msg`、`InferenceChunk.msg`、`Sentence.msg`、`EmotionResult.msg`
- 服务：`CaptureImage.srv`

修改接口时必须同步：

1. 更新 `.msg` / `.srv` 文件
2. 重新 `colcon build`
3. 更新依赖这些接口的包测试

## 3. Topic/Service 约束

1. Topic 命名保持"模块前缀 + 语义名"。

### Topic 列表（双脑架构）

| Topic | 发布者 | 订阅者 | 消息类型 |
|-------|--------|--------|----------|
| `/audio/wake_word` | buddy_audio | buddy_brain | std_msgs/String |
| `/audio/asr_text` | buddy_audio | buddy_brain | std_msgs/String |
| `/brain/request` | buddy_brain | buddy_cloud, buddy_local_llm | InferenceRequest |
| `/inference/local_chunk` | buddy_local_llm | buddy_brain | InferenceChunk |
| `/inference/cloud_chunk` | buddy_cloud | buddy_brain | InferenceChunk |
| `/brain/sentence` | buddy_brain | buddy_audio | Sentence |
| `/audio/tts_done` | buddy_audio | buddy_brain | std_msgs/Empty |
| `/vision/emotion/result` | buddy_vision | buddy_brain | EmotionResult |

### Service 列表

| Service | 服务端 | 客户端 | 类型 |
|---------|--------|--------|------|
| `/vision/emotion/capture` | buddy_vision | buddy_brain | CaptureImage |

2. 服务用于显式请求-响应场景，事件流优先 Topic。
3. 新增字段必须向后兼容。

## 4. 时序原则

1. 状态与上下文编排由 `buddy_brain` 负责。
2. 本地模型（ollama）先响应，brain 立即送 TTS 播放。
3. 云端模型（Doubao）到达后，brain 打断本地播放，替换为云端回复。
4. `buddy_audio` 发送播放完成信号，驱动主流程。
```

- [ ] **Step 3: Update plan.md — add dual-brain phase**

Append to `docs/plan.md`, after the `## TODO` section and before `## 提交历史`:

Add a new completed phase after 阶段 3:

```markdown

### 阶段 4：双脑架构（本地 + 云端推理）

| 项目 | 状态 | 提交 | 说明 |
|------|------|------|------|
| 消息重命名 Cloud→Inference | ✅ | — | CloudRequest→InferenceRequest, CloudChunk→InferenceChunk |
| buddy_cloud 适配新消息 | ✅ | — | topic/消息类型重命名 |
| buddy_local_llm 新建 | ✅ | — | ollama 流式推理组件 |
| buddy_brain 双流合并 | ✅ | — | 本地先播、云端替换逻辑 |
| buddy_app 加载 local_llm | ✅ | — | 组件加载列表+配置 |
| 文档同步 | ✅ | — | architecture, protocol, plan |

**变更文件：**
- `src/buddy_interfaces/msg/` — InferenceRequest.msg, InferenceChunk.msg (重命名)
- `src/buddy_local_llm/` — 整个包（新建）
- `src/buddy_cloud/` — 消息类型和 topic 名更新
- `src/buddy_brain/` — 双流订阅+替换逻辑
- `src/buddy_app/src/buddy_main.cpp` — 组件列表
- `src/buddy_app/params/local_llm.yaml` — 本地 LLM 配置
- `docs/` — 架构、协议、计划文档
```

Update the "当前架构" section at the bottom:

```markdown
## 当前架构

```
buddy_audio (ALSA + Sherpa-ONNX KWS/ASR + TTS stub)
    ↓ /audio/wake_word, /audio/asr_text
buddy_brain (状态机 + 对话上下文 + 切句 + 情绪仲裁 + 双流合并)
    ↓ /brain/request                  ↑ /inference/local_chunk (快速回复)
    ├─→ buddy_local_llm (ollama)      ↑ /inference/cloud_chunk (精细回复)
    └─→ buddy_cloud (Doubao API)
buddy_brain → /brain/sentence → buddy_audio (TTS)
buddy_vision (Haar + ONNX 情绪识别) → /vision/emotion
```
```

- [ ] **Step 4: Commit**

```bash
git add docs/
git commit -m "feat(module): [PRO-10000] Update docs for dual-brain architecture"
```

---

### Task 7: Format and lint

- [ ] **Step 1: Run format and lint**

Run: `./format_and_lint.sh`
Expected: All files formatted, no lint errors.

- [ ] **Step 2: Check formatting**

Run: `./format_and_lint.sh --check`
Expected: Exit code 0 (all clean).

- [ ] **Step 3: Commit if any changes**

```bash
git add -A
git commit -m "feat(module): [PRO-10000] Format and lint for dual-brain architecture"
```
(Only if there are changes to commit.)
