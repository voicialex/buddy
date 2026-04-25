# Multi-Camera Vision Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add multi-camera video capture with independent inference pipelines to `buddy_vision`, supporting two USB cameras (expression + game) with double-buffered frame transfer between capture and inference threads.

**Architecture:** Single `VisionPipelineNode` (LifecycleNode) internally manages multiple `CameraWorker` instances. Each worker owns a capture thread, inference thread, `FrameBuffer`, and `ModelInterface`. Capture and inference communicate via mutex-protected double buffer — capture never blocks inference and vice versa.

**Tech Stack:** C++17, OpenCV (`cv::VideoCapture`), ROS 2 LifecycleNode, `rclcpp_components`, GTest

**Spec:** `docs/superpowers/specs/2026-04-30-multi-camera-vision-design.md`

**Repository root:** All file paths below are relative to `/home/seb/buddy_ws/buddy_robot/`. The source tree lives under `src/` (e.g. `src/buddy_vision/`).

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/buddy_vision/include/buddy_vision/frame_buffer.hpp` | Create | Mutex-protected double buffer for frame transfer between threads |
| `src/buddy_vision/include/buddy_vision/model_interface.hpp` | Create | Abstract model interface + `MockModel` for PC development |
| `src/buddy_vision/include/buddy_vision/camera_worker.hpp` | Create | Per-camera capture + inference thread management |
| `src/buddy_vision/include/buddy_vision/vision_pipeline_node.hpp` | Modify | Multi-camera support, per-camera publishers/services |
| `src/buddy_vision/src/vision_pipeline_node.cpp` | Modify | Read config, instantiate CameraWorkers |
| `src/buddy_vision/test/test_frame_buffer.cpp` | Create | FrameBuffer unit tests |
| `src/buddy_vision/test/test_model_interface.cpp` | Create | MockModel unit tests |
| `src/buddy_vision/test/test_vision_node.cpp` | Modify | Update lifecycle tests |
| `src/buddy_vision/CMakeLists.txt` | Modify | Add OpenCV, new test targets, install models/ |
| `src/buddy_vision/package.xml` | Modify | Add dependencies |
| `src/buddy_vision/models/.gitignore` | Create | Exclude model binaries from git |
| `src/buddy_vision/models/expression/.gitkeep` | Create | Placeholder directory |
| `src/buddy_vision/models/game/.gitkeep` | Create | Placeholder directory |
| `src/buddy_app/params/vision.yaml` | Modify | New multi-camera config structure |
| `src/buddy_state_machine/src/state_machine_node.cpp` | Modify | Update topic `/vision/expression` → `/vision/expression/result` |

---

## Task Dependency Graph

```
Task 1 (FrameBuffer) ──┐
                        ├─→ Task 3 (CameraWorker) ─→ Task 4 (Node refactor) ─→ Task 5 (Build system) ─→ Task 6 (Config)
Task 2 (MockModel)  ───┘
```

Tasks 1 and 2 are independent. Task 3 depends on both. Each subsequent task depends on the previous.

---

### Task 1: FrameBuffer

**Files:**
- Create: `src/buddy_vision/include/buddy_vision/frame_buffer.hpp`
- Create: `src/buddy_vision/test/test_frame_buffer.cpp`
- Modify: `src/buddy_vision/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `src/buddy_vision/test/test_frame_buffer.cpp`:

```cpp
#include "buddy_vision/frame_buffer.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

TEST(FrameBufferTest, WriteThenSnapshotReturnsLatest) {
  FrameBuffer buf;
  cv::Mat frame1 = cv::Mat::zeros(480, 640, CV_8UC3);
  frame1.at<cv::Vec3b>(0, 0) = {1, 2, 3};
  buf.write(frame1.clone());

  cv::Mat out;
  ASSERT_TRUE(buf.snapshot(out));
  EXPECT_EQ(out.at<cv::Vec3b>(0, 0), cv::Vec3b(1, 2, 3));
}

TEST(FrameBufferTest, SnapshotBeforeWriteReturnsFalse) {
  FrameBuffer buf;
  cv::Mat out;
  EXPECT_FALSE(buf.snapshot(out));
}

TEST(FrameBufferTest, OverwriteKeepsLatest) {
  FrameBuffer buf;
  cv::Mat frame1 = cv::Mat::zeros(480, 640, CV_8UC3);
  frame1.at<cv::Vec3b>(0, 0) = {10, 20, 30};
  cv::Mat frame2 = cv::Mat::zeros(480, 640, CV_8UC3);
  frame2.at<cv::Vec3b>(0, 0) = {40, 50, 60};

  buf.write(frame1.clone());
  buf.write(frame2.clone());

  cv::Mat out;
  ASSERT_TRUE(buf.snapshot(out));
  EXPECT_EQ(out.at<cv::Vec3b>(0, 0), cv::Vec3b(40, 50, 60));
}

TEST(FrameBufferTest, ConcurrentWriteAndSnapshot) {
  FrameBuffer buf;
  std::atomic<bool> done{false};
  int write_count = 0;

  std::thread writer([&] {
    for (int i = 1; i <= 100; ++i) {
      cv::Mat frame = cv::Mat::zeros(480, 640, CV_8UC3);
      frame.at<cv::Vec3b>(0, 0) = {static_cast<uchar>(i % 256), 0, 0};
      buf.write(std::move(frame));
      write_count = i;
    }
    done = true;
  });

  std::thread reader([&] {
    while (!done) {
      cv::Mat out;
      buf.snapshot(out);
    }
  });

  writer.join();
  reader.join();
  EXPECT_EQ(write_count, 100);
}
```

- [ ] **Step 2: Run build to verify it fails**

Run: `cd /home/seb/buddy_ws/buddy_robot && ./build.sh --packages-select buddy_vision 2>&1 | tail -10`
Expected: BUILD FAILED — `frame_buffer.hpp` not found

- [ ] **Step 3: Write FrameBuffer implementation**

Create `src/buddy_vision/include/buddy_vision/frame_buffer.hpp`:

```cpp
#pragma once

#include <mutex>
#include <opencv2/opencv.hpp>

class FrameBuffer {
public:
  void write(cv::Mat frame) {
    std::lock_guard<std::mutex> lock(mtx_);
    buffers_[back_] = std::move(frame);
    back_ = 1 - back_;
    has_frame_ = true;
  }

  bool snapshot(cv::Mat &out) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!has_frame_) {
      return false;
    }
    out = buffers_[1 - back_].clone();
    return true;
  }

private:
  cv::Mat buffers_[2];
  int back_{0};
  bool has_frame_{false};
  std::mutex mtx_;
};
```

- [ ] **Step 4: Add test target to CMakeLists.txt**

Add `find_package(OpenCV REQUIRED)` **before** the `add_library` call (outside `if(BUILD_TESTING)`), near the other `find_package` lines.

Add `OpenCV` to the `ament_target_dependencies` of `vision_component`.

Inside the `if(BUILD_TESTING)` block, add:

```cmake
ament_add_gtest(test_frame_buffer test/test_frame_buffer.cpp)
target_include_directories(
  test_frame_buffer
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(test_frame_buffer PUBLIC cxx_std_17)
ament_target_dependencies(test_frame_buffer OpenCV)
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd /home/seb/buddy_ws/buddy_robot && ./build.sh --packages-select buddy_vision && colcon test --packages-select buddy_vision && colcon test-result --verbose`
Expected: All FrameBuffer tests PASS

- [ ] **Step 6: Commit**

```bash
cd /home/seb/buddy_ws/buddy_robot
git add src/buddy_vision/include/buddy_vision/frame_buffer.hpp \
        src/buddy_vision/test/test_frame_buffer.cpp \
        src/buddy_vision/CMakeLists.txt
git commit -m "feat(module): [PRO-10000] Add FrameBuffer double-buffer class"
```

---

### Task 2: ModelInterface + MockModel

**Depends on:** Task 1 (both modify CMakeLists.txt — Task 2 adds another test target)

**Files:**
- Create: `src/buddy_vision/include/buddy_vision/model_interface.hpp`
- Create: `src/buddy_vision/test/test_model_interface.cpp`
- Modify: `src/buddy_vision/CMakeLists.txt` (add test target)

- [ ] **Step 1: Write the failing test**

Create `src/buddy_vision/test/test_model_interface.cpp`:

```cpp
#include "buddy_vision/model_interface.hpp"
#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

TEST(MockModelTest, LoadReturnsTrue) {
  MockModel model;
  EXPECT_TRUE(model.load("/any/path"));
}

TEST(MockModelTest, InferenceReturnsFixedResult) {
  MockModel model;
  model.load("");
  cv::Mat frame = cv::Mat::zeros(224, 224, CV_8UC3);
  auto result = model.inference(frame);
  EXPECT_EQ(result.label, "neutral");
  EXPECT_FLOAT_EQ(result.confidence, 0.95f);
}

TEST(MockModelTest, UnloadDoesNotCrash) {
  MockModel model;
  model.load("");
  model.unload();
  SUCCEED();
}
```

- [ ] **Step 2: Run build to verify it fails**

Run: `cd /home/seb/buddy_ws/buddy_robot && ./build.sh --packages-select buddy_vision 2>&1 | tail -10`
Expected: BUILD FAILED — `model_interface.hpp` not found

- [ ] **Step 3: Write ModelInterface + MockModel**

Create `src/buddy_vision/include/buddy_vision/model_interface.hpp`:

```cpp
#pragma once

#include <string>
#include <opencv2/opencv.hpp>

struct ModelResult {
  std::string label;
  float confidence;
};

class ModelInterface {
public:
  virtual ~ModelInterface() = default;
  virtual bool load(const std::string &model_path) = 0;
  virtual ModelResult inference(const cv::Mat &frame) = 0;
  virtual void unload() = 0;
};

class MockModel : public ModelInterface {
public:
  bool load(const std::string & /*model_path*/) override { return true; }
  ModelResult inference(const cv::Mat & /*frame*/) override {
    return {"neutral", 0.95f};
  }
  void unload() override {}
};
```

- [ ] **Step 4: Add test target to CMakeLists.txt**

Inside the `if(BUILD_TESTING)` block, after `test_frame_buffer`:

```cmake
ament_add_gtest(test_model_interface test/test_model_interface.cpp)
target_include_directories(
  test_model_interface
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(test_model_interface PUBLIC cxx_std_17)
ament_target_dependencies(test_model_interface OpenCV)
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd /home/seb/buddy_ws/buddy_robot && ./build.sh --packages-select buddy_vision && colcon test --packages-select buddy_vision && colcon test-result --verbose`
Expected: All tests PASS (FrameBuffer + MockModel)

- [ ] **Step 6: Commit**

```bash
cd /home/seb/buddy_ws/buddy_robot
git add src/buddy_vision/include/buddy_vision/model_interface.hpp \
        src/buddy_vision/test/test_model_interface.cpp \
        src/buddy_vision/CMakeLists.txt
git commit -m "feat(module): [PRO-10000] Add ModelInterface and MockModel"
```

---

### Task 3: CameraWorker

**Depends on:** Task 1 + Task 2

**Files:**
- Create: `src/buddy_vision/include/buddy_vision/camera_worker.hpp`

- [ ] **Step 1: Write CameraWorker**

Create `src/buddy_vision/include/buddy_vision/camera_worker.hpp`:

```cpp
#pragma once

#include "buddy_vision/frame_buffer.hpp"
#include "buddy_vision/model_interface.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <opencv2/opencv.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <thread>

struct CameraConfig {
  std::string name;
  std::string device_path;
  int frame_width;
  int frame_height;
  std::string model_path;
  int model_input_width;
  int model_input_height;
  int inference_interval_ms;
};

class CameraWorker {
public:
  using ResultCallback =
      std::function<void(const std::string &label, float confidence)>;

  CameraWorker(const CameraConfig &config, rclcpp::Logger logger)
      : config_(config), logger_(logger),
        model_(std::make_unique<MockModel>()), running_(false) {}

  bool start() {
    cap_.open(config_.device_path, cv::CAP_V4L2);
    if (!cap_.isOpened()) {
      RCLCPP_WARN(logger_, "CameraWorker [%s]: failed to open %s",
                  config_.name.c_str(), config_.device_path.c_str());
      return false;
    }
    cap_.set(cv::CAP_PROP_FRAME_WIDTH, config_.frame_width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.frame_height);

    if (!model_->load(config_.model_path)) {
      RCLCPP_ERROR(logger_, "CameraWorker [%s]: failed to load model %s",
                   config_.name.c_str(), config_.model_path.c_str());
      model_loaded_ = false;
    } else {
      model_loaded_ = true;
    }

    running_ = true;
    capture_thread_ = std::thread(&CameraWorker::capture_loop, this);
    inference_thread_ = std::thread(&CameraWorker::inference_loop, this);
    RCLCPP_INFO(logger_, "CameraWorker [%s]: started", config_.name.c_str());
    return true;
  }

  void stop() {
    running_ = false;
    if (capture_thread_.joinable()) {
      capture_thread_.join();
    }
    if (inference_thread_.joinable()) {
      inference_thread_.join();
    }
    if (cap_.isOpened()) {
      cap_.release();
    }
    model_->unload();
    RCLCPP_INFO(logger_, "CameraWorker [%s]: stopped", config_.name.c_str());
  }

  bool get_latest_frame(cv::Mat &out) { return buffer_.snapshot(out); }

  void set_result_callback(ResultCallback cb) { result_cb_ = std::move(cb); }

private:
  void capture_loop() {
    cv::Mat frame;
    while (running_) {
      if (!cap_.isOpened()) {
        try_reconnect();
        continue;
      }
      if (!cap_.read(frame)) {
        RCLCPP_WARN(logger_, "CameraWorker [%s]: read failed, reconnecting",
                    config_.name.c_str());
        cap_.release();
        try_reconnect();
        continue;
      }
      buffer_.write(std::move(frame));
    }
  }

  void inference_loop() {
    while (running_) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(config_.inference_interval_ms));
      if (!running_ || !model_loaded_) {
        continue;
      }
      cv::Mat frame;
      if (!buffer_.snapshot(frame)) {
        continue;
      }
      cv::Mat processed;
      cv::resize(frame, processed,
                 cv::Size(config_.model_input_width, config_.model_input_height));
      cv::cvtColor(processed, processed, cv::COLOR_BGR2RGB);
      processed.convertTo(processed, CV_32F, 1.0 / 255.0);

      auto result = model_->inference(processed);
      if (result_cb_) {
        result_cb_(result.label, result.confidence);
      }
    }
  }

  void try_reconnect() {
    for (int attempt = 0; attempt < 3 && running_; ++attempt) {
      int delay_ms = 1000 * (attempt + 1);
      RCLCPP_INFO(logger_, "CameraWorker [%s]: reconnect attempt %d in %dms",
                  config_.name.c_str(), attempt + 1, delay_ms);
      auto deadline = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(delay_ms);
      while (running_ && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (!running_) return;
      cap_.open(config_.device_path, cv::CAP_V4L2);
      if (cap_.isOpened()) {
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, config_.frame_width);
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.frame_height);
        RCLCPP_INFO(logger_, "CameraWorker [%s]: reconnected",
                    config_.name.c_str());
        return;
      }
    }
    RCLCPP_ERROR(logger_,
                 "CameraWorker [%s]: failed to reconnect after 3 attempts",
                 config_.name.c_str());
  }

  CameraConfig config_;
  rclcpp::Logger logger_;
  FrameBuffer buffer_;
  std::unique_ptr<ModelInterface> model_;
  bool model_loaded_{false};
  std::atomic<bool> running_;
  std::thread capture_thread_;
  std::thread inference_thread_;
  cv::VideoCapture cap_;
  ResultCallback result_cb_;
};
```

- [ ] **Step 2: Build to verify compilation**

Run: `cd /home/seb/buddy_ws/buddy_robot && ./build.sh --packages-select buddy_vision`
Expected: BUILD SUCCEEDED

- [ ] **Step 3: Commit**

```bash
cd /home/seb/buddy_ws/buddy_robot
git add src/buddy_vision/include/buddy_vision/camera_worker.hpp
git commit -m "feat(module): [PRO-10000] Add CameraWorker class"
```

---

### Task 4: Refactor VisionPipelineNode

**Depends on:** Task 3

**Files:**
- Modify: `src/buddy_vision/include/buddy_vision/vision_pipeline_node.hpp`
- Modify: `src/buddy_vision/src/vision_pipeline_node.cpp`
- Modify: `src/buddy_vision/test/test_vision_node.cpp`

- [ ] **Step 1: Rewrite the header**

Replace `src/buddy_vision/include/buddy_vision/vision_pipeline_node.hpp` with:

```cpp
#pragma once

#include "buddy_vision/camera_worker.hpp"

#include <buddy_interfaces/msg/expression_result.hpp>
#include <buddy_interfaces/srv/capture_image.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class VisionPipelineNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit VisionPipelineNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  std::vector<std::string> discover_camera_names();
  CameraConfig load_camera_config(const std::string &name);
  void handle_capture(
      const std::string &camera_name,
      const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request>
          request,
      std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> response);

  std::map<std::string, std::unique_ptr<CameraWorker>> workers_;
  std::map<std::string,
           rclcpp::Publisher<buddy_interfaces::msg::ExpressionResult>::SharedPtr>
      result_pubs_;
  std::map<std::string,
           rclcpp::Service<buddy_interfaces::srv::CaptureImage>::SharedPtr>
      capture_srvs_;
};
```

- [ ] **Step 2: Rewrite the implementation**

Replace `src/buddy_vision/src/vision_pipeline_node.cpp` with:

```cpp
#include "buddy_vision/vision_pipeline_node.hpp"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <set>

VisionPipelineNode::VisionPipelineNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("vision", options) {}

CallbackReturn
VisionPipelineNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: configuring");

  auto camera_names = discover_camera_names();
  if (camera_names.empty()) {
    RCLCPP_WARN(get_logger(), "No camera configs found in parameters");
    // Not an error — node can exist with zero cameras (e.g. in tests)
  }

  std::string pkg_path;
  try {
    pkg_path = ament_index_cpp::get_package_share_directory("buddy_vision");
  } catch (const std::exception &e) {
    RCLCPP_WARN(get_logger(),
                "Could not resolve buddy_vision package path: %s", e.what());
    pkg_path = "";
  }

  for (auto &name : camera_names) {
    CameraConfig cfg = load_camera_config(name);
    if (!pkg_path.empty()) {
      cfg.model_path = pkg_path + "/" + cfg.model_path;
    }

    result_pubs_[name] =
        create_publisher<buddy_interfaces::msg::ExpressionResult>(
            "/vision/" + name + "/result", 10);

    capture_srvs_[name] =
        create_service<buddy_interfaces::srv::CaptureImage>(
            "/vision/" + name + "/capture",
            [this, n = name](auto req, auto res) {
              handle_capture(n, req, res);
            });

    workers_[name] = std::make_unique<CameraWorker>(cfg, get_logger());

    workers_[name]->set_result_callback(
        [this, pub = result_pubs_[name]](const std::string &label,
                                          float confidence) {
          auto msg = buddy_interfaces::msg::ExpressionResult();
          msg.expression = label;
          msg.confidence = confidence;
          msg.timestamp = now();
          pub->publish(msg);
        });

    RCLCPP_INFO(get_logger(), "Configured camera: [%s] → %s",
                name.c_str(), cfg.device_path.c_str());
  }

  return CallbackReturn::SUCCESS;
}

CallbackReturn
VisionPipelineNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: activating");
  for (auto &[name, worker] : workers_) {
    if (!worker->start()) {
      RCLCPP_WARN(get_logger(), "Camera [%s] failed to start, skipping",
                  name.c_str());
    }
  }
  return CallbackReturn::SUCCESS;
}

CallbackReturn
VisionPipelineNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: deactivating");
  for (auto &[name, worker] : workers_) {
    worker->stop();
  }
  return CallbackReturn::SUCCESS;
}

CallbackReturn
VisionPipelineNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: cleaning up");
  workers_.clear();
  result_pubs_.clear();
  capture_srvs_.clear();
  return CallbackReturn::SUCCESS;
}

CallbackReturn
VisionPipelineNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: shutting down");
  return CallbackReturn::SUCCESS;
}

CallbackReturn
VisionPipelineNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "VisionPipelineNode: error");
  return CallbackReturn::SUCCESS;
}

std::vector<std::string> VisionPipelineNode::discover_camera_names() {
  // ROS 2 flattens nested YAML maps into dotted parameter names:
  //   cameras.expression.device_path → parameter "cameras.expression.device_path"
  // Use list_parameters with prefix "cameras" and depth 2 to get
  // "cameras.expression.device_path", then extract "expression".
  auto result = this->list_parameters({"cameras"}, 2);
  std::set<std::string> names;
  for (auto &p : result.names) {
    // "cameras.expression.device_path" → extract "expression"
    // prefix = "cameras.", then take everything up to the next "."
    const std::string prefix = "cameras.";
    if (p.rfind(prefix, 0) != 0) continue;
    auto rest = p.substr(prefix.size());
    auto dot = rest.find('.');
    if (dot == std::string::npos) continue;
    names.insert(rest.substr(0, dot));
  }
  return {names.begin(), names.end()};
}

CameraConfig
VisionPipelineNode::load_camera_config(const std::string &name) {
  auto prefix = "cameras." + name + ".";
  CameraConfig cfg;
  cfg.name = name;
  cfg.device_path = this->get_parameter(prefix + "device_path").as_string();
  cfg.frame_width = this->get_parameter(prefix + "frame_width").as_int();
  cfg.frame_height = this->get_parameter(prefix + "frame_height").as_int();
  cfg.model_path = this->get_parameter(prefix + "model_path").as_string();
  cfg.model_input_width =
      this->get_parameter(prefix + "model_input_width").as_int();
  cfg.model_input_height =
      this->get_parameter(prefix + "model_input_height").as_int();
  cfg.inference_interval_ms =
      this->get_parameter(prefix + "inference_interval_ms").as_int();
  return cfg;
}

void VisionPipelineNode::handle_capture(
    const std::string &camera_name,
    const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request>,
    std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> response) {
  auto it = workers_.find(camera_name);
  if (it == workers_.end()) {
    RCLCPP_WARN(get_logger(), "Capture requested for unknown camera: %s",
                camera_name.c_str());
    return;
  }

  cv::Mat frame;
  if (it->second->get_latest_frame(frame) && !frame.empty()) {
    auto &img = response->image;
    img.height = frame.rows;
    img.width = frame.cols;
    img.encoding = "bgr8";
    img.step = static_cast<uint32_t>(frame.cols * frame.elemSize());
    img.data.assign(frame.datastart, frame.dataend);
  }
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(VisionPipelineNode)
```

- [ ] **Step 3: Update test**

Replace `src/buddy_vision/test/test_vision_node.cpp`:

```cpp
#include "buddy_vision/vision_pipeline_node.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

class VisionNodeTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<VisionPipelineNode>(); }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<VisionPipelineNode> node_;
};

TEST_F(VisionNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), std::string("vision"));
}

TEST_F(VisionNodeTest, ConfigureWithNoCameras) {
  // No YAML params loaded → discover_camera_names returns empty →
  // on_configure succeeds with zero workers
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
}

TEST_F(VisionNodeTest, FullLifecycleSequence) {
  EXPECT_STREQ(node_->configure().label().c_str(), "inactive");
  EXPECT_STREQ(node_->activate().label().c_str(), "active");
  EXPECT_STREQ(node_->deactivate().label().c_str(), "inactive");
  EXPECT_STREQ(node_->cleanup().label().c_str(), "unconfigured");
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
```

**Key design decisions in the test:**
- `rclcpp::init` / `rclcpp::shutdown` called once in `main()`, not per test case
- No YAML params loaded → `discover_camera_names()` returns empty vector → `on_configure` succeeds gracefully with zero cameras
- This matches the original test structure

- [ ] **Step 4: Build and run all tests**

Run: `cd /home/seb/buddy_ws/buddy_robot && ./build.sh --packages-select buddy_vision && colcon test --packages-select buddy_vision && colcon test-result --verbose`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
cd /home/seb/buddy_ws/buddy_robot
git add src/buddy_vision/include/buddy_vision/vision_pipeline_node.hpp \
        src/buddy_vision/src/vision_pipeline_node.cpp \
        src/buddy_vision/test/test_vision_node.cpp
git commit -m "feat(module): [PRO-10000] Refactor VisionPipelineNode for multi-camera"
```

---

### Task 5: Build System + Model Directory

**Depends on:** Task 4

**Files:**
- Modify: `src/buddy_vision/CMakeLists.txt` (add `ament_index_cpp` dep + install models/)
- Modify: `src/buddy_vision/package.xml` (add deps)
- Create: `src/buddy_vision/models/.gitignore`
- Create: `src/buddy_vision/models/expression/.gitkeep`
- Create: `src/buddy_vision/models/game/.gitkeep`

- [ ] **Step 1: Update package.xml**

Replace the `<depend>` block in `src/buddy_vision/package.xml` to add `ament_index_cpp`:

```xml
  <depend>rclcpp</depend>
  <depend>rclcpp_lifecycle</depend>
  <depend>rclcpp_components</depend>
  <depend>sensor_msgs</depend>
  <depend>buddy_interfaces</depend>
  <depend>ament_index_cpp</depend>
```

**Note:** `OpenCV` is pulled in via `find_package(OpenCV)` in CMake. `cv_bridge` is NOT needed (we do manual Mat→Image conversion), so do not add it.

- [ ] **Step 2: Update CMakeLists.txt — complete final version**

Replace the entire `src/buddy_vision/CMakeLists.txt` with:

```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_vision)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(OpenCV REQUIRED)

add_library(vision_component SHARED src/vision_pipeline_node.cpp)
target_include_directories(
  vision_component PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                          $<INSTALL_INTERFACE:include>)
target_compile_features(vision_component PUBLIC cxx_std_17)
ament_target_dependencies(vision_component rclcpp rclcpp_lifecycle
                          rclcpp_components sensor_msgs buddy_interfaces
                          ament_index_cpp OpenCV)
rclcpp_components_register_nodes(vision_component "VisionPipelineNode")
install(
  TARGETS vision_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

install(DIRECTORY models/
        DESTINATION share/${PROJECT_NAME}/models
        PATTERN ".gitignore" EXCLUDE
        PATTERN ".gitkeep" EXCLUDE)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_frame_buffer test/test_frame_buffer.cpp)
  target_include_directories(
    test_frame_buffer
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_frame_buffer PUBLIC cxx_std_17)
  ament_target_dependencies(test_frame_buffer OpenCV)

  ament_add_gtest(test_model_interface test/test_model_interface.cpp)
  target_include_directories(
    test_model_interface
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_model_interface PUBLIC cxx_std_17)
  ament_target_dependencies(test_model_interface OpenCV)

  ament_add_gtest(test_vision_node test/test_vision_node.cpp
                  src/vision_pipeline_node.cpp)
  target_include_directories(
    test_vision_node
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_vision_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_vision_node rclcpp rclcpp_lifecycle
                            rclcpp_components buddy_interfaces
                            ament_index_cpp OpenCV)
endif()
ament_package()
```

- [ ] **Step 3: Create models directory structure**

```bash
cd /home/seb/buddy_ws/buddy_robot
mkdir -p src/buddy_vision/models/expression
mkdir -p src/buddy_vision/models/game
touch src/buddy_vision/models/expression/.gitkeep
touch src/buddy_vision/models/game/.gitkeep
```

Create `src/buddy_vision/models/.gitignore`:

```
# Model binaries — too large for git
*.rknn
*.onnx
*.pt
```

- [ ] **Step 4: Full build and test**

Run: `cd /home/seb/buddy_ws/buddy_robot && ./build.sh --packages-select buddy_interfaces buddy_vision && colcon test --packages-select buddy_vision && colcon test-result --verbose`
Expected: BUILD SUCCEEDED, all tests PASS

- [ ] **Step 5: Commit**

```bash
cd /home/seb/buddy_ws/buddy_robot
git add src/buddy_vision/CMakeLists.txt \
        src/buddy_vision/package.xml \
        src/buddy_vision/models/
git commit -m "feat(module): [PRO-10000] Update build system and add models directory"
```

---

### Task 6: YAML Config + Breaking Change Fix

**Depends on:** Task 5

**Files:**
- Modify: `src/buddy_app/params/vision.yaml`
- Modify: `src/buddy_state_machine/src/state_machine_node.cpp`

- [ ] **Step 1: Update vision.yaml**

Replace `src/buddy_app/params/vision.yaml` with:

```yaml
vision:
  ros__parameters:
    cameras:
      expression:
        device_path: "/dev/video0"
        frame_width: 640
        frame_height: 480
        model_path: "models/expression/model.rknn"
        model_input_width: 224
        model_input_height: 224
        inference_interval_ms: 500
      game:
        device_path: "/dev/video2"
        frame_width: 640
        frame_height: 480
        model_path: "models/game/model.rknn"
        model_input_width: 224
        model_input_height: 224
        inference_interval_ms: 500
```

- [ ] **Step 2: Fix state_machine topic subscription**

In `src/buddy_state_machine/src/state_machine_node.cpp`, find:

```cpp
expression_sub_ =
    create_subscription<buddy_interfaces::msg::ExpressionResult>(
        "/vision/expression", 10,
```

Change `"/vision/expression"` to `"/vision/expression/result"`:

```cpp
expression_sub_ =
    create_subscription<buddy_interfaces::msg::ExpressionResult>(
        "/vision/expression/result", 10,
```

- [ ] **Step 3: Full workspace build and test**

Run: `cd /home/seb/buddy_ws/buddy_robot && ./build.sh && colcon test && colcon test-result --verbose`
Expected: ALL packages build, all tests PASS

- [ ] **Step 4: Commit**

```bash
cd /home/seb/buddy_ws/buddy_robot
git add src/buddy_app/params/vision.yaml \
        src/buddy_state_machine/src/state_machine_node.cpp
git commit -m "feat(module): [PRO-10000] Update vision config and fix topic names"
```

---

## Summary

| Task | Description | Key Files |
|------|-------------|-----------|
| 1 | FrameBuffer double-buffer | `frame_buffer.hpp`, `test_frame_buffer.cpp` |
| 2 | ModelInterface + MockModel | `model_interface.hpp`, `test_model_interface.cpp` |
| 3 | CameraWorker threads | `camera_worker.hpp` |
| 4 | VisionPipelineNode refactor | `vision_pipeline_node.hpp/.cpp`, `test_vision_node.cpp` |
| 5 | Build system + models dir | `CMakeLists.txt`, `package.xml`, `models/` |
| 6 | Config + breaking change | `vision.yaml`, `state_machine_node.cpp` |
