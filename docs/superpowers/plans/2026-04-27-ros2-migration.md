# ROS2 Jazzy Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate buddy_robot from custom single-process framework (EventBus + IModule + dlopen) to ROS2 Jazzy (system-installed), keeping all 6 module functions.

**Architecture:** 6 LifecycleNode packages replacing current IModule .so modules, communicating via ROS2 Topics/Services. Uses system-installed ROS2 Jazzy (`/opt/ros/jazzy`). Built with colcon. Deployed natively on x86 (dev) and aarch64/RK3588 (prod).

**Tech Stack:** C++17, ROS2 Jazzy, colcon, Fast-DDS, rclcpp_lifecycle, rclcpp_components, image_transport, cv_bridge, gtest, launch_testing

**Spec:** `docs/superpowers/specs/2026-04-27-ros2-migration-design.md`

---

## File Structure (all paths relative to repo root)

### New files to create:

```
third_party/ros2/jazzy/
├── ros2_minimal.repos                  # Filtered repos file (for reference)
├── src/                                # Minimal ROS2 source (reference only, not used in build)

scripts/
└── filter_ros2_repos.py                # Script to filter full repos to minimal set

src/buddy_robot/
├── buddy_interfaces/
│   ├── msg/
│   │   ├── UserInput.msg
│   │   ├── CloudChunk.msg
│   │   ├── Sentence.msg
│   │   ├── ExpressionResult.msg
│   │   └── DisplayCommand.msg
│   ├── srv/
│   │   └── CaptureImage.srv
│   ├── CMakeLists.txt
│   └── package.xml
├── buddy_audio/
│   ├── include/buddy_audio/
│   │   └── audio_pipeline_node.hpp
│   ├── src/
│   │   └── audio_pipeline_node.cpp
│   ├── test/
│   │   └── test_audio_node.cpp
│   ├── CMakeLists.txt
│   └── package.xml
├── buddy_vision/
│   ├── include/buddy_vision/
│   │   └── vision_pipeline_node.hpp
│   ├── src/
│   │   └── vision_pipeline_node.cpp
│   ├── test/
│   │   └── test_vision_node.cpp
│   ├── CMakeLists.txt
│   └── package.xml
├── buddy_cloud/
│   ├── include/buddy_cloud/
│   │   └── cloud_client_node.hpp
│   ├── src/
│   │   └── cloud_client_node.cpp
│   ├── test/
│   │   └── test_cloud_node.cpp
│   ├── CMakeLists.txt
│   └── package.xml
├── buddy_state_machine/
│   ├── include/buddy_state_machine/
│   │   └── state_machine_node.hpp
│   ├── src/
│   │   └── state_machine_node.cpp
│   ├── test/
│   │   └── test_state_machine_node.cpp
│   ├── CMakeLists.txt
│   └── package.xml
├── buddy_dialog/
│   ├── include/buddy_dialog/
│   │   └── dialog_manager_node.hpp
│   ├── src/
│   │   └── dialog_manager_node.cpp
│   ├── test/
│   │   └── test_dialog_node.cpp
│   ├── CMakeLists.txt
│   └── package.xml
├── buddy_sentence/
│   ├── include/buddy_sentence/
│   │   └── sentence_segmenter_node.hpp
│   ├── src/
│   │   └── sentence_segmenter_node.cpp
│   ├── test/
│   │   └── test_sentence_node.cpp
│   ├── CMakeLists.txt
│   └── package.xml
└── buddy_bringup/
    ├── launch/
    │   └── buddy.launch.py
    ├── params/
    │   └── buddy_params.yaml
    ├── CMakeLists.txt
    └── package.xml
```

### Files to keep (docs only):
- `docs/` — all documentation stays
- `README.md` — update for ROS2 build instructions

### Files to remove (old framework):
- `src/app/` — IModule, ModuleRuntime, ModuleRegistry, main.cpp
- `src/core/` — EventBus, ThreadPool
- `modules/` — all 6 old .so modules
- `config/robot_config.json` — replaced by YAML params
- `CMakeLists.txt` (root) — replaced by colcon workspace
- `conanfile.py` or `conanfile.txt` — replaced by ROS2 deps

---

## Task 1: Create Feature Branch and Clean Old Source

**Goal:** Create `feature/ros2-migration` branch, remove old framework code, keep docs.

**Files:**
- Remove: `src/app/`, `src/core/`, `modules/`, `config/robot_config.json`, root `CMakeLists.txt`, `conanfile.*`
- Keep: `docs/`, `README.md`, `.git/`

- [ ] **Step 1: Create feature branch**

```bash
git checkout -b feature/ros2-migration main
```

Expected: New branch created from main.

- [ ] **Step 2: Remove old source directories**

```bash
git rm -r src/app/ src/core/ modules/
```

- [ ] **Step 3: Remove old build files**

```bash
git rm -f CMakeLists.txt config/robot_config.json
# Also remove conanfile if it exists
git rm -f conanfile.py conanfile.txt 2>/dev/null || true
```

- [ ] **Step 4: Create new directory structure**

```bash
mkdir -p third_party/ros2/jazzy
mkdir -p scripts
mkdir -p src/buddy_robot
```

- [ ] **Step 5: Add .gitkeep to preserve empty dirs**

```bash
touch third_party/ros2/jazzy/.gitkeep
touch src/buddy_robot/.gitkeep
```

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(build): [PRO-10000] Remove old framework and create ROS2 workspace structure"
```

---

## Task 2: ROS2 Minimal Repos Filter Script

**Goal:** Write a Python script that filters the full ROS2 `ros2.repos` down to only the packages we need. (Used for reference; actual build uses system-installed Jazzy.)

**Files:**
- Create: `scripts/filter_ros2_repos.py`
- Create: `scripts/package_list.txt`

- [ ] **Step 1: Create the package list file**

Create `scripts/package_list.txt` listing all required ROS2 package repository names (one per line):

```
# scripts/package_list.txt
# Ament build system
ament_cmake
ament_package
ament_index_cpp
# Rosidl message generation
rosidl
rosidl_dds
rosidl_defaults
rosidl_python
rosidl_runtime
rosidl_typesupport
fastrtps
foonathan_memory_vendor
# Core interfaces
builtin_interfaces
std_msgs
sensor_msgs
geometry_msgs
lifecycle_msgs
composition_interfaces
service_msgs
# RMW middleware
rcutils
rcl
rcl_yaml_param_parser
rmw
rmw_fastrtps
rmw_implementation
tracetools
osrf_pycommon
# C++ client library
rclcpp
rclcpp_lifecycle
rclcpp_components
class_loader
pluginlib
rcpputils
# Image pipeline
image_transport
image_common
vision_opencv
camera_calibration_parsers
```

- [ ] **Step 2: Write the filter script**

```python
#!/usr/bin/env python3
"""Filter full ros2.repos to minimal set needed by buddy_robot."""
import sys
import yaml

def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <input.repos> <package_list.txt> <output.repos>")
        sys.exit(1)

    input_repos, pkg_list_file, output_repos = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(pkg_list_file) as f:
        packages = {line.strip() for line in f
                    if line.strip() and not line.startswith('#')}

    with open(input_repos) as f:
        repos = yaml.safe_load(f)

    filtered = {'repositories': {}}
    for name, spec in repos.get('repositories', {}).items():
        # Match if repo name starts with a known package name
        pkg_name = name.split('/')[-1].replace('.git', '').replace('_', '-')
        # Also try the raw name
        if (name in packages or pkg_name in packages or
                any(p in name for p in packages)):
            filtered['repositories'][name] = spec

    print(f"Kept {len(filtered['repositories'])}/{len(repos.get('repositories', {}))} repos")

    with open(output_repos, 'w') as f:
        yaml.dump(filtered, f, default_flow_style=False, sort_keys=False)

if __name__ == '__main__':
    main()
```

- [ ] **Step 3: Test the script syntax**

```bash
python3 -c "import ast; ast.parse(open('scripts/filter_ros2_repos.py').read()); print('OK')"
```

Expected: `OK`

- [ ] **Step 4: Commit**

```bash
git add scripts/
git commit -m "feat(scripts): [PRO-10000] Add ROS2 repos filter script and package list"
```

---

## Task 3: Fetch and Build Minimal ROS2 Source

**Goal:** Install ROS2 Jazzy from apt and verify required packages.

**Files:** None (system packages)

**Prerequisites:** x86 Ubuntu 24.04, ROS2 Jazzy installed (`/opt/ros/jazzy`)

- [ ] **Step 1: Install ROS2 Jazzy**

```bash
sudo apt install ros-jazzy-ros-base ros-jazzy-rclcpp-lifecycle \
  ros-jazzy-rclcpp-components ros-jazzy-image-transport
```

- [ ] **Step 2: Verify installation**

```bash
source /opt/ros/jazzy/setup.bash
ros2 pkg list | grep rclcpp_lifecycle
```

Expected: `rclcpp_lifecycle`, `rclcpp_components` listed.

- [ ] **Step 3: Commit**

Note: No ROS2 source code is committed. The system-installed ROS2 Jazzy is used directly.

---

## Task 4: buddy_interfaces Package (Custom Messages)

**Goal:** Create the custom message and service definitions.

**Files:**
- Create: `src/buddy_robot/buddy_interfaces/msg/UserInput.msg`
- Create: `src/buddy_robot/buddy_interfaces/msg/CloudChunk.msg`
- Create: `src/buddy_robot/buddy_interfaces/msg/Sentence.msg`
- Create: `src/buddy_robot/buddy_interfaces/msg/ExpressionResult.msg`
- Create: `src/buddy_robot/buddy_interfaces/msg/DisplayCommand.msg`
- Create: `src/buddy_robot/buddy_interfaces/srv/CaptureImage.srv`
- Create: `src/buddy_robot/buddy_interfaces/CMakeLists.txt`
- Create: `src/buddy_robot/buddy_interfaces/package.xml`

- [ ] **Step 1: Create package directory structure**

```bash
mkdir -p src/buddy_robot/buddy_interfaces/msg
mkdir -p src/buddy_robot/buddy_interfaces/srv
```

- [ ] **Step 2: Write message definitions**

`src/buddy_robot/buddy_interfaces/msg/UserInput.msg`:
```
string text
string session_id
builtin_interfaces/Time timestamp
```

`src/buddy_robot/buddy_interfaces/msg/CloudChunk.msg`:
```
string session_id
string chunk_text
bool is_final
```

`src/buddy_robot/buddy_interfaces/msg/Sentence.msg`:
```
string session_id
string text
uint32 index
```

`src/buddy_robot/buddy_interfaces/msg/ExpressionResult.msg`:
```
string expression
float32 confidence
builtin_interfaces/Time timestamp
```

`src/buddy_robot/buddy_interfaces/msg/DisplayCommand.msg`:
```
string command
string payload
```

`src/buddy_robot/buddy_interfaces/srv/CaptureImage.srv`:
```
# Request: empty
---
# Response
sensor_msgs/msg/Image image
```

- [ ] **Step 3: Write package.xml**

`src/buddy_robot/buddy_interfaces/package.xml`:
```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>buddy_interfaces</name>
  <version>0.1.0</version>
  <description>Custom message and service definitions for Buddy Robot</description>
  <maintainer email="dev@buddy.robot">seb</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>
  <buildtool_depend>rosidl_default_generators</buildtool_depend>

  <depend>builtin_interfaces</depend>
  <depend>sensor_msgs</depend>
  <depend>std_msgs</depend>

  <exec_depend>rosidl_default_runtime</exec_depend>

  <member_of_group>rosidl_interface_packages</member_of_group>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 4: Write CMakeLists.txt**

`src/buddy_robot/buddy_interfaces/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_interfaces)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/UserInput.msg"
  "msg/CloudChunk.msg"
  "msg/Sentence.msg"
  "msg/ExpressionResult.msg"
  "msg/DisplayCommand.msg"
  "srv/CaptureImage.srv"
  DEPENDENCIES builtin_interfaces sensor_msgs std_msgs
)

ament_package()
```

- [ ] **Step 5: Build and verify**

```bash
source /opt/ros/jazzy/setup.bash
cd /home/seb/buddy_robot
colcon build --packages-select buddy_interfaces
```

Expected: Build succeeds, message headers generated.

- [ ] **Step 6: Verify message generation**

```bash
source install/setup.bash
ros2 interface show buddy_interfaces/msg/UserInput
```

Expected:
```
string text
string session_id
builtin_interfaces/Time timestamp
```

- [ ] **Step 7: Commit**

```bash
git add src/buddy_robot/buddy_interfaces/
git commit -m "feat(interfaces): [PRO-10000] Add custom message and service definitions"
```

---

## Task 5: buddy_audio LifecycleNode

**Goal:** Create the audio pipeline as a ROS2 LifecycleNode. Handles wake word detection, ASR, and TTS playback.

**Files:**
- Create: `src/buddy_robot/buddy_audio/package.xml`
- Create: `src/buddy_robot/buddy_audio/CMakeLists.txt`
- Create: `src/buddy_robot/buddy_audio/include/buddy_audio/audio_pipeline_node.hpp`
- Create: `src/buddy_robot/buddy_audio/src/audio_pipeline_node.cpp`
- Create: `src/buddy_robot/buddy_audio/test/test_audio_node.cpp`

**Topics published:** `/audio/wake_word`, `/audio/asr_text`
**Topics subscribed:** `/dialog/sentence` (triggers TTS)
**Publishes on TTS complete:** `/audio/tts_done`

- [ ] **Step 1: Write package.xml**

`src/buddy_robot/buddy_audio/package.xml`:
```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>buddy_audio</name>
  <version>0.1.0</version>
  <description>Audio pipeline: wake word, ASR, TTS</description>
  <maintainer email="dev@buddy.robot">seb</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>rclcpp_lifecycle</depend>
  <depend>std_msgs</depend>
  <depend>buddy_interfaces</depend>

  <test_depend>ament_cmake_gtest</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 2: Write CMakeLists.txt**

`src/buddy_robot/buddy_audio/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_audio)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(std_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_executable(audio_node
  src/audio_pipeline_node.cpp
)
target_include_directories(audio_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(audio_node PUBLIC cxx_std_17)
ament_target_dependencies(audio_node
  rclcpp rclcpp_lifecycle std_msgs buddy_interfaces
)
install(TARGETS audio_node
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_audio_node test/test_audio_node.cpp)
  target_include_directories(test_audio_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_compile_features(test_audio_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_audio_node
    rclcpp rclcpp_lifecycle buddy_interfaces
  )
endif()

ament_package()
```

- [ ] **Step 3: Write header**

`src/buddy_robot/buddy_audio/include/buddy_audio/audio_pipeline_node.hpp`:
```cpp
#pragma once

#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/empty.hpp>
#include <buddy_interfaces/msg/sentence.hpp>

class AudioPipelineNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit AudioPipelineNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void on_sentence(const buddy_interfaces::msg::Sentence &msg);

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr wake_word_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr asr_text_pub_;
  rclcpp::Publisher<std_msgs::msg::Empty>::SharedPtr tts_done_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::Sentence>::SharedPtr sentence_sub_;
};
```

- [ ] **Step 4: Write implementation (skeleton with mock behavior)**

`src/buddy_robot/buddy_audio/src/audio_pipeline_node.cpp`:
```cpp
#include "buddy_audio/audio_pipeline_node.hpp"

AudioPipelineNode::AudioPipelineNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("audio", options) {}

CallbackReturn AudioPipelineNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: configuring");

  wake_word_pub_ = create_publisher<std_msgs::msg::String>("/audio/wake_word", 10);
  asr_text_pub_ = create_publisher<std_msgs::msg::String>("/audio/asr_text", 10);
  tts_done_pub_ = create_publisher<std_msgs::msg::Empty>("/audio/tts_done", 10);

  sentence_sub_ = create_subscription<buddy_interfaces::msg::Sentence>(
      "/dialog/sentence", 10,
      std::bind(&AudioPipelineNode::on_sentence, this, std::placeholders::_1));

  return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: activating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: deactivating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: cleaning up");
  wake_word_pub_.reset();
  asr_text_pub_.reset();
  tts_done_pub_.reset();
  sentence_sub_.reset();
  return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: shutting down");
  return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "AudioPipelineNode: error");
  return CallbackReturn::SUCCESS;
}

void AudioPipelineNode::on_sentence(const buddy_interfaces::msg::Sentence &msg) {
  RCLCPP_INFO(get_logger(), "TTS: playing sentence [%u]: %s",
              msg.index, msg.text.c_str());
  // TODO: integrate real TTS engine
  // Mock: immediately signal TTS done
  std_msgs::msg::Empty done;
  tts_done_pub_->publish(done);
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<AudioPipelineNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
```

- [ ] **Step 5: Write unit test**

`src/buddy_robot/buddy_audio/test/test_audio_node.cpp`:
```cpp
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include "buddy_audio/audio_pipeline_node.hpp"

class AudioNodeTest : public ::testing::Test {
protected:
  void SetUp() override {
    node_ = std::make_shared<AudioPipelineNode>();
  }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<AudioPipelineNode> node_;
};

TEST_F(AudioNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), "audio");
}

TEST_F(AudioNodeTest, ConfigureTransitionsToInactive) {
  auto state = node_->configure();
  EXPECT_EQ(state.id(), lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
}

TEST_F(AudioNodeTest, FullLifecycleSequence) {
  EXPECT_EQ(node_->configure().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->activate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->deactivate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->cleanup().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
```

- [ ] **Step 6: Build and test**

```bash
source /opt/ros/jazzy/setup.bash
source install/setup.bash
colcon build --packages-select buddy_audio
colcon test --packages-select buddy_audio
```

Expected: Build succeeds, all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/buddy_robot/buddy_audio/
git commit -m "feat(audio): [PRO-10000] Add audio pipeline LifecycleNode with tests"
```

---

## Task 6: buddy_vision LifecycleNode

**Goal:** Create the vision pipeline as a LifecycleNode. Handles camera capture and expression recognition via RKNN model.

**Files:**
- Create: `src/buddy_robot/buddy_vision/package.xml`
- Create: `src/buddy_robot/buddy_vision/CMakeLists.txt`
- Create: `src/buddy_robot/buddy_vision/include/buddy_vision/vision_pipeline_node.hpp`
- Create: `src/buddy_robot/buddy_vision/src/vision_pipeline_node.cpp`
- Create: `src/buddy_robot/buddy_vision/test/test_vision_node.cpp`

**Topics published:** `/vision/expression`
**Service provided:** `/vision/capture` (CaptureImage)

- [ ] **Step 1: Write package.xml**

`src/buddy_robot/buddy_vision/package.xml`:
```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>buddy_vision</name>
  <version>0.1.0</version>
  <description>Vision pipeline: camera capture and expression recognition</description>
  <maintainer email="dev@buddy.robot">seb</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>rclcpp_lifecycle</depend>
  <depend>sensor_msgs</depend>
  <depend>buddy_interfaces</depend>

  <test_depend>ament_cmake_gtest</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 2: Write CMakeLists.txt**

`src/buddy_robot/buddy_vision/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_vision)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_executable(vision_node
  src/vision_pipeline_node.cpp
)
target_include_directories(vision_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(vision_node PUBLIC cxx_std_17)
ament_target_dependencies(vision_node
  rclcpp rclcpp_lifecycle sensor_msgs buddy_interfaces
)
install(TARGETS vision_node
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_vision_node test/test_vision_node.cpp)
  target_include_directories(test_vision_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_compile_features(test_vision_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_vision_node
    rclcpp rclcpp_lifecycle buddy_interfaces
  )
endif()

ament_package()
```

- [ ] **Step 3: Write header**

`src/buddy_robot/buddy_vision/include/buddy_vision/vision_pipeline_node.hpp`:
```cpp
#pragma once

#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <buddy_interfaces/msg/expression_result.hpp>
#include <buddy_interfaces/srv/capture_image.hpp>

class VisionPipelineNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit VisionPipelineNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void handle_capture(
      const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request> request,
      std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> response);

  rclcpp::Publisher<buddy_interfaces::msg::ExpressionResult>::SharedPtr expression_pub_;
  rclcpp::Service<buddy_interfaces::srv::CaptureImage>::SharedPtr capture_srv_;
};
```

- [ ] **Step 4: Write implementation**

`src/buddy_robot/buddy_vision/src/vision_pipeline_node.cpp`:
```cpp
#include "buddy_vision/vision_pipeline_node.hpp"

VisionPipelineNode::VisionPipelineNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("vision", options) {}

CallbackReturn VisionPipelineNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: configuring");

  expression_pub_ =
      create_publisher<buddy_interfaces::msg::ExpressionResult>("/vision/expression", 10);

  capture_srv_ = create_service<buddy_interfaces::srv::CaptureImage>(
      "/vision/capture",
      std::bind(&VisionPipelineNode::handle_capture, this,
                std::placeholders::_1, std::placeholders::_2));

  return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: activating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: deactivating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: cleaning up");
  expression_pub_.reset();
  capture_srv_.reset();
  return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: shutting down");
  return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "VisionPipelineNode: error");
  return CallbackReturn::SUCCESS;
}

void VisionPipelineNode::handle_capture(
    const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request>,
    std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> response) {
  RCLCPP_INFO(get_logger(), "Capture requested, running expression recognition");

  // TODO: integrate real camera capture + RKNN model inference
  // Mock: return neutral expression
  auto result = buddy_interfaces::msg::ExpressionResult();
  result.expression = "neutral";
  result.confidence = 0.95f;
  result.timestamp = now();
  expression_pub_->publish(result);

  // Mock: empty image in response
  // Real: fill with captured frame
  (void)response;
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<VisionPipelineNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
```

- [ ] **Step 5: Write unit test**

`src/buddy_robot/buddy_vision/test/test_vision_node.cpp`:
```cpp
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include "buddy_vision/vision_pipeline_node.hpp"

class VisionNodeTest : public ::testing::Test {
protected:
  void SetUp() override {
    node_ = std::make_shared<VisionPipelineNode>();
  }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<VisionPipelineNode> node_;
};

TEST_F(VisionNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), "vision");
}

TEST_F(VisionNodeTest, FullLifecycleSequence) {
  EXPECT_EQ(node_->configure().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->activate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->deactivate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->cleanup().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
```

- [ ] **Step 6: Build and test**

```bash
colcon build --packages-select buddy_vision
colcon test --packages-select buddy_vision
```

Expected: Build succeeds, all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/buddy_robot/buddy_vision/
git commit -m "feat(vision): [PRO-10000] Add vision pipeline LifecycleNode with tests"
```

---

## Task 7: buddy_cloud LifecycleNode

**Goal:** Create the cloud client as a LifecycleNode. Manages WebSocket connection to gateway, streams responses.

**Files:**
- Create: `src/buddy_robot/buddy_cloud/package.xml`
- Create: `src/buddy_robot/buddy_cloud/CMakeLists.txt`
- Create: `src/buddy_robot/buddy_cloud/include/buddy_cloud/cloud_client_node.hpp`
- Create: `src/buddy_robot/buddy_cloud/src/cloud_client_node.cpp`
- Create: `src/buddy_robot/buddy_cloud/test/test_cloud_node.cpp`

**Topics published:** `/dialog/cloud_response` (CloudChunk)
**Topics subscribed:** `/dialog/user_input` (triggers cloud request)

- [ ] **Step 1: Write package.xml**

`src/buddy_robot/buddy_cloud/package.xml`:
```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>buddy_cloud</name>
  <version>0.1.0</version>
  <description>Cloud client: WebSocket gateway connection</description>
  <maintainer email="dev@buddy.robot">seb</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>rclcpp_lifecycle</depend>
  <depend>buddy_interfaces</depend>

  <test_depend>ament_cmake_gtest</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 2: Write CMakeLists.txt**

`src/buddy_robot/buddy_cloud/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_cloud)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_executable(cloud_node
  src/cloud_client_node.cpp
)
target_include_directories(cloud_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(cloud_node PUBLIC cxx_std_17)
ament_target_dependencies(cloud_node
  rclcpp rclcpp_lifecycle buddy_interfaces
)
install(TARGETS cloud_node
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_cloud_node test/test_cloud_node.cpp)
  target_include_directories(test_cloud_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_compile_features(test_cloud_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_cloud_node
    rclcpp rclcpp_lifecycle buddy_interfaces
  )
endif()

ament_package()
```

- [ ] **Step 3: Write header**

`src/buddy_robot/buddy_cloud/include/buddy_cloud/cloud_client_node.hpp`:
```cpp
#pragma once

#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <buddy_interfaces/msg/user_input.hpp>
#include <buddy_interfaces/msg/cloud_chunk.hpp>

class CloudClientNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit CloudClientNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void on_user_input(const buddy_interfaces::msg::UserInput &msg);

  rclcpp::Publisher<buddy_interfaces::msg::CloudChunk>::SharedPtr cloud_response_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::UserInput>::SharedPtr user_input_sub_;
};
```

- [ ] **Step 4: Write implementation**

`src/buddy_robot/buddy_cloud/src/cloud_client_node.cpp`:
```cpp
#include "buddy_cloud/cloud_client_node.hpp"

CloudClientNode::CloudClientNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("cloud", options) {}

CallbackReturn CloudClientNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: configuring");

  cloud_response_pub_ =
      create_publisher<buddy_interfaces::msg::CloudChunk>("/dialog/cloud_response", 10);

  user_input_sub_ = create_subscription<buddy_interfaces::msg::UserInput>(
      "/dialog/user_input", 10,
      std::bind(&CloudClientNode::on_user_input, this, std::placeholders::_1));

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
  user_input_sub_.reset();
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

void CloudClientNode::on_user_input(const buddy_interfaces::msg::UserInput &msg) {
  RCLCPP_INFO(get_logger(), "Cloud request for session %s: %s",
              msg.session_id.c_str(), msg.text.c_str());

  // TODO: integrate real WebSocket connection to gateway
  // Mock: echo back a response
  auto chunk = buddy_interfaces::msg::CloudChunk();
  chunk.session_id = msg.session_id;
  chunk.chunk_text = "Hello, I am Buddy!";
  chunk.is_final = true;
  cloud_response_pub_->publish(chunk);
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<CloudClientNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
```

- [ ] **Step 5: Write unit test**

`src/buddy_robot/buddy_cloud/test/test_cloud_node.cpp`:
```cpp
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include "buddy_cloud/cloud_client_node.hpp"

class CloudNodeTest : public ::testing::Test {
protected:
  void SetUp() override {
    node_ = std::make_shared<CloudClientNode>();
  }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<CloudClientNode> node_;
};

TEST_F(CloudNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), "cloud");
}

TEST_F(CloudNodeTest, FullLifecycleSequence) {
  EXPECT_EQ(node_->configure().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->activate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->deactivate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->cleanup().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
```

- [ ] **Step 6: Build and test**

```bash
colcon build --packages-select buddy_cloud
colcon test --packages-select buddy_cloud
```

Expected: Build succeeds, all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/buddy_robot/buddy_cloud/
git commit -m "feat(cloud): [PRO-10000] Add cloud client LifecycleNode with tests"
```

---

## Task 8: buddy_state_machine LifecycleNode

**Goal:** Create the state machine as a LifecycleNode. Orchestrates all other modules by subscribing to events and triggering actions.

**Files:**
- Create: `src/buddy_robot/buddy_state_machine/package.xml`
- Create: `src/buddy_robot/buddy_state_machine/CMakeLists.txt`
- Create: `src/buddy_robot/buddy_state_machine/include/buddy_state_machine/state_machine_node.hpp`
- Create: `src/buddy_robot/buddy_state_machine/src/state_machine_node.cpp`
- Create: `src/buddy_robot/buddy_state_machine/test/test_state_machine_node.cpp`

**Topics subscribed:** `/audio/wake_word`, `/audio/asr_text`, `/audio/tts_done`, `/vision/expression`, `/dialog/cloud_response`
**Topics published:** `/dialog/user_input`, `/display/command`
**Service client:** `/vision/capture`

- [ ] **Step 1: Write package.xml**

`src/buddy_robot/buddy_state_machine/package.xml`:
```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>buddy_state_machine</name>
  <version>0.1.0</version>
  <description>State machine orchestrator for Buddy Robot</description>
  <maintainer email="dev@buddy.robot">seb</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>rclcpp_lifecycle</depend>
  <depend>std_msgs</depend>
  <depend>buddy_interfaces</depend>

  <test_depend>ament_cmake_gtest</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 2: Write CMakeLists.txt**

`src/buddy_robot/buddy_state_machine/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_state_machine)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(std_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_executable(state_machine_node
  src/state_machine_node.cpp
)
target_include_directories(state_machine_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(state_machine_node PUBLIC cxx_std_17)
ament_target_dependencies(state_machine_node
  rclcpp rclcpp_lifecycle std_msgs buddy_interfaces
)
install(TARGETS state_machine_node
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_state_machine_node test/test_state_machine_node.cpp)
  target_include_directories(test_state_machine_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_compile_features(test_state_machine_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_state_machine_node
    rclcpp rclcpp_lifecycle buddy_interfaces
  )
endif()

ament_package()
```

- [ ] **Step 3: Write header**

`src/buddy_robot/buddy_state_machine/include/buddy_state_machine/state_machine_node.hpp`:
```cpp
#pragma once

#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/empty.hpp>
#include <buddy_interfaces/msg/user_input.hpp>
#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/expression_result.hpp>
#include <buddy_interfaces/msg/display_command.hpp>
#include <buddy_interfaces/srv/capture_image.hpp>

#include <string>

class StateMachineNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit StateMachineNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  enum class State { IDLE, LISTENING, THINKING, SPEAKING };

  void on_wake_word(const std_msgs::msg::String &msg);
  void on_asr_text(const std_msgs::msg::String &msg);
  void on_expression(const buddy_interfaces::msg::ExpressionResult &msg);
  void on_cloud_response(const buddy_interfaces::msg::CloudChunk &msg);
  void on_tts_done(const std_msgs::msg::Empty &msg);

  void transition(State new_state);

  State state_{State::IDLE};
  std::string session_id_;

  rclcpp::Publisher<buddy_interfaces::msg::UserInput>::SharedPtr user_input_pub_;
  rclcpp::Publisher<buddy_interfaces::msg::DisplayCommand>::SharedPtr display_pub_;
  rclcpp::Client<buddy_interfaces::srv::CaptureImage>::SharedPtr capture_client_;

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr wake_word_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr asr_text_sub_;
  rclcpp::Subscription<buddy_interfaces::msg::ExpressionResult>::SharedPtr expression_sub_;
  rclcpp::Subscription<buddy_interfaces::msg::CloudChunk>::SharedPtr cloud_response_sub_;
  rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr tts_done_sub_;
};
```

- [ ] **Step 4: Write implementation**

`src/buddy_robot/buddy_state_machine/src/state_machine_node.cpp`:
```cpp
#include "buddy_state_machine/state_machine_node.hpp"
#include <sstream>
#include <chrono>

StateMachineNode::StateMachineNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("state_machine", options) {}

CallbackReturn StateMachineNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "StateMachineNode: configuring");

  user_input_pub_ =
      create_publisher<buddy_interfaces::msg::UserInput>("/dialog/user_input", 10);
  display_pub_ =
      create_publisher<buddy_interfaces::msg::DisplayCommand>("/display/command", 10);
  capture_client_ =
      create_client<buddy_interfaces::srv::CaptureImage>("/vision/capture");

  wake_word_sub_ = create_subscription<std_msgs::msg::String>(
      "/audio/wake_word", 10,
      std::bind(&StateMachineNode::on_wake_word, this, std::placeholders::_1));
  asr_text_sub_ = create_subscription<std_msgs::msg::String>(
      "/audio/asr_text", 10,
      std::bind(&StateMachineNode::on_asr_text, this, std::placeholders::_1));
  expression_sub_ = create_subscription<buddy_interfaces::msg::ExpressionResult>(
      "/vision/expression", 10,
      std::bind(&StateMachineNode::on_expression, this, std::placeholders::_1));
  cloud_response_sub_ = create_subscription<buddy_interfaces::msg::CloudChunk>(
      "/dialog/cloud_response", 10,
      std::bind(&StateMachineNode::on_cloud_response, this, std::placeholders::_1));
  tts_done_sub_ = create_subscription<std_msgs::msg::Empty>(
      "/audio/tts_done", 10,
      std::bind(&StateMachineNode::on_tts_done, this, std::placeholders::_1));

  return CallbackReturn::SUCCESS;
}

CallbackReturn StateMachineNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "StateMachineNode: activating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn StateMachineNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "StateMachineNode: deactivating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn StateMachineNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "StateMachineNode: cleaning up");
  user_input_pub_.reset();
  display_pub_.reset();
  capture_client_.reset();
  wake_word_sub_.reset();
  asr_text_sub_.reset();
  expression_sub_.reset();
  cloud_response_sub_.reset();
  tts_done_sub_.reset();
  return CallbackReturn::SUCCESS;
}

CallbackReturn StateMachineNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "StateMachineNode: shutting down");
  return CallbackReturn::SUCCESS;
}

CallbackReturn StateMachineNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "StateMachineNode: error");
  return CallbackReturn::SUCCESS;
}

void StateMachineNode::on_wake_word(const std_msgs::msg::String &) {
  RCLCPP_INFO(get_logger(), "Wake word detected");
  transition(State::LISTENING);
}

void StateMachineNode::on_asr_text(const std_msgs::msg::String &msg) {
  if (state_ != State::LISTENING) return;

  RCLCPP_INFO(get_logger(), "ASR: %s", msg.data.c_str());

  // Generate session ID
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()).count();
  session_id_ = "sess-" + std::to_string(ms);

  // TODO: optionally trigger vision capture here
  // capture_client_->async_send_request(...);

  // Send to dialog
  auto input = buddy_interfaces::msg::UserInput();
  input.text = msg.data;
  input.session_id = session_id_;
  input.timestamp = rclcpp::Clock().now();
  user_input_pub_->publish(input);

  transition(State::THINKING);
}

void StateMachineNode::on_expression(const buddy_interfaces::msg::ExpressionResult &msg) {
  RCLCPP_INFO(get_logger(), "Expression: %s (%.2f)",
              msg.expression.c_str(), msg.confidence);
  // TODO: use expression to adjust interaction strategy
}

void StateMachineNode::on_cloud_response(const buddy_interfaces::msg::CloudChunk &msg) {
  if (msg.is_final) {
    RCLCPP_INFO(get_logger(), "Cloud stream complete for session %s",
                msg.session_id.c_str());
  }
}

void StateMachineNode::on_tts_done(const std_msgs::msg::Empty &) {
  if (state_ == State::SPEAKING) {
    RCLCPP_INFO(get_logger(), "TTS done, returning to idle");
    transition(State::IDLE);
  }
}

void StateMachineNode::transition(State new_state) {
  static const char *names[] = {"IDLE", "LISTENING", "THINKING", "SPEAKING"};
  RCLCPP_INFO(get_logger(), "State: %s -> %s", names[static_cast<int>(state_)],
              names[static_cast<int>(new_state)]);
  state_ = new_state;
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<StateMachineNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
```

- [ ] **Step 5: Write unit test**

`src/buddy_robot/buddy_state_machine/test/test_state_machine_node.cpp`:
```cpp
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include "buddy_state_machine/state_machine_node.hpp"

class StateMachineNodeTest : public ::testing::Test {
protected:
  void SetUp() override {
    node_ = std::make_shared<StateMachineNode>();
  }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<StateMachineNode> node_;
};

TEST_F(StateMachineNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), "state_machine");
}

TEST_F(StateMachineNodeTest, FullLifecycleSequence) {
  EXPECT_EQ(node_->configure().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->activate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->deactivate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->cleanup().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
```

- [ ] **Step 6: Build and test**

```bash
colcon build --packages-select buddy_state_machine
colcon test --packages-select buddy_state_machine
```

Expected: Build succeeds, all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/buddy_robot/buddy_state_machine/
git commit -m "feat(state_machine): [PRO-10000] Add state machine orchestrator LifecycleNode"
```

---

## Task 9: buddy_dialog LifecycleNode

**Goal:** Create the dialog manager as a LifecycleNode. Receives user input, forwards to cloud, manages conversation context.

**Files:**
- Create: `src/buddy_robot/buddy_dialog/package.xml`
- Create: `src/buddy_robot/buddy_dialog/CMakeLists.txt`
- Create: `src/buddy_robot/buddy_dialog/include/buddy_dialog/dialog_manager_node.hpp`
- Create: `src/buddy_robot/buddy_dialog/src/dialog_manager_node.cpp`
- Create: `src/buddy_robot/buddy_dialog/test/test_dialog_node.cpp`

**Topics subscribed:** `/dialog/user_input`
**Topics published:** `/dialog/cloud_response` (forwarded from cloud module processing)

Note: The dialog module receives `/dialog/user_input` and manages conversation context (history, persona). It forwards requests to the cloud client which publishes `/dialog/cloud_response`.

- [ ] **Step 1: Write package.xml**

`src/buddy_robot/buddy_dialog/package.xml`:
```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>buddy_dialog</name>
  <version>0.1.0</version>
  <description>Dialog manager: conversation context and routing</description>
  <maintainer email="dev@buddy.robot">seb</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>rclcpp_lifecycle</depend>
  <depend>buddy_interfaces</depend>

  <test_depend>ament_cmake_gtest</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 2: Write CMakeLists.txt**

`src/buddy_robot/buddy_dialog/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_dialog)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_executable(dialog_node
  src/dialog_manager_node.cpp
)
target_include_directories(dialog_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(dialog_node PUBLIC cxx_std_17)
ament_target_dependencies(dialog_node
  rclcpp rclcpp_lifecycle buddy_interfaces
)
install(TARGETS dialog_node
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_dialog_node test/test_dialog_node.cpp)
  target_include_directories(test_dialog_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_compile_features(test_dialog_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_dialog_node
    rclcpp rclcpp_lifecycle buddy_interfaces
  )
endif()

ament_package()
```

- [ ] **Step 3: Write header**

`src/buddy_robot/buddy_dialog/include/buddy_dialog/dialog_manager_node.hpp`:
```cpp
#pragma once

#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <buddy_interfaces/msg/user_input.hpp>

class DialogManagerNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit DialogManagerNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  void on_user_input(const buddy_interfaces::msg::UserInput &msg);

  rclcpp::Subscription<buddy_interfaces::msg::UserInput>::SharedPtr user_input_sub_;
};
```

- [ ] **Step 4: Write implementation**

`src/buddy_robot/buddy_dialog/src/dialog_manager_node.cpp`:
```cpp
#include "buddy_dialog/dialog_manager_node.hpp"

DialogManagerNode::DialogManagerNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("dialog", options) {}

CallbackReturn DialogManagerNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "DialogManagerNode: configuring");

  user_input_sub_ = create_subscription<buddy_interfaces::msg::UserInput>(
      "/dialog/user_input", 10,
      std::bind(&DialogManagerNode::on_user_input, this, std::placeholders::_1));

  return CallbackReturn::SUCCESS;
}

CallbackReturn DialogManagerNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "DialogManagerNode: activating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn DialogManagerNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "DialogManagerNode: deactivating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn DialogManagerNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "DialogManagerNode: cleaning up");
  user_input_sub_.reset();
  return CallbackReturn::SUCCESS;
}

CallbackReturn DialogManagerNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "DialogManagerNode: shutting down");
  return CallbackReturn::SUCCESS;
}

CallbackReturn DialogManagerNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "DialogManagerNode: error");
  return CallbackReturn::SUCCESS;
}

void DialogManagerNode::on_user_input(const buddy_interfaces::msg::UserInput &msg) {
  RCLCPP_INFO(get_logger(), "Dialog input [%s]: %s",
              msg.session_id.c_str(), msg.text.c_str());
  // TODO: manage conversation context (history, persona)
  // Cloud client subscribes to same /dialog/user_input topic
  // and handles the actual WebSocket request
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<DialogManagerNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
```

- [ ] **Step 5: Write unit test**

`src/buddy_robot/buddy_dialog/test/test_dialog_node.cpp`:
```cpp
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include "buddy_dialog/dialog_manager_node.hpp"

class DialogNodeTest : public ::testing::Test {
protected:
  void SetUp() override {
    node_ = std::make_shared<DialogManagerNode>();
  }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<DialogManagerNode> node_;
};

TEST_F(DialogNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), "dialog");
}

TEST_F(DialogNodeTest, FullLifecycleSequence) {
  EXPECT_EQ(node_->configure().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->activate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->deactivate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->cleanup().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
```

- [ ] **Step 6: Build and test**

```bash
colcon build --packages-select buddy_dialog
colcon test --packages-select buddy_dialog
```

Expected: Build succeeds, all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/buddy_robot/buddy_dialog/
git commit -m "feat(dialog): [PRO-10000] Add dialog manager LifecycleNode with tests"
```

---

## Task 10: buddy_sentence LifecycleNode

**Goal:** Create the sentence segmenter as a LifecycleNode. Receives cloud streaming chunks, segments into sentences for TTS.

**Files:**
- Create: `src/buddy_robot/buddy_sentence/package.xml`
- Create: `src/buddy_robot/buddy_sentence/CMakeLists.txt`
- Create: `src/buddy_robot/buddy_sentence/include/buddy_sentence/sentence_segmenter_node.hpp`
- Create: `src/buddy_robot/buddy_sentence/src/sentence_segmenter_node.cpp`
- Create: `src/buddy_robot/buddy_sentence/test/test_sentence_node.cpp`

**Topics subscribed:** `/dialog/cloud_response`
**Topics published:** `/dialog/sentence`

- [ ] **Step 1: Write package.xml**

`src/buddy_robot/buddy_sentence/package.xml`:
```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>buddy_sentence</name>
  <version>0.1.0</version>
  <description>Sentence segmenter: chunks cloud stream into TTS-ready sentences</description>
  <maintainer email="dev@buddy.robot">seb</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>rclcpp_lifecycle</depend>
  <depend>buddy_interfaces</depend>

  <test_depend>ament_cmake_gtest</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 2: Write CMakeLists.txt**

`src/buddy_robot/buddy_sentence/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_sentence)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_executable(sentence_node
  src/sentence_segmenter_node.cpp
)
target_include_directories(sentence_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(sentence_node PUBLIC cxx_std_17)
ament_target_dependencies(sentence_node
  rclcpp rclcpp_lifecycle buddy_interfaces
)
install(TARGETS sentence_node
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_sentence_node test/test_sentence_node.cpp)
  target_include_directories(test_sentence_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_compile_features(test_sentence_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_sentence_node
    rclcpp rclcpp_lifecycle buddy_interfaces
  )
endif()

ament_package()
```

- [ ] **Step 3: Write header**

`src/buddy_robot/buddy_sentence/include/buddy_sentence/sentence_segmenter_node.hpp`:
```cpp
#pragma once

#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <rclcpp/rclcpp.hpp>
#include <buddy_interfaces/msg/cloud_chunk.hpp>
#include <buddy_interfaces/msg/sentence.hpp>
#include <string>

class SentenceSegmenterNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit SentenceSegmenterNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

  // Public for testing
  std::vector<std::string> segment(const std::string &text);

private:
  void on_cloud_chunk(const buddy_interfaces::msg::CloudChunk &msg);
  void flush_buffer(const std::string &session_id);

  rclcpp::Publisher<buddy_interfaces::msg::Sentence>::SharedPtr sentence_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::CloudChunk>::SharedPtr cloud_sub_;

  std::string buffer_;
  uint32_t sentence_index_{0};
  std::string current_session_;
};
```

- [ ] **Step 4: Write implementation**

`src/buddy_robot/buddy_sentence/src/sentence_segmenter_node.cpp`:
```cpp
#include "buddy_sentence/sentence_segmenter_node.hpp"

SentenceSegmenterNode::SentenceSegmenterNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("sentence", options) {}

CallbackReturn SentenceSegmenterNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "SentenceSegmenterNode: configuring");

  sentence_pub_ =
      create_publisher<buddy_interfaces::msg::Sentence>("/dialog/sentence", 10);
  cloud_sub_ = create_subscription<buddy_interfaces::msg::CloudChunk>(
      "/dialog/cloud_response", 10,
      std::bind(&SentenceSegmenterNode::on_cloud_chunk, this, std::placeholders::_1));

  return CallbackReturn::SUCCESS;
}

CallbackReturn SentenceSegmenterNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "SentenceSegmenterNode: activating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn SentenceSegmenterNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "SentenceSegmenterNode: deactivating");
  return CallbackReturn::SUCCESS;
}

CallbackReturn SentenceSegmenterNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "SentenceSegmenterNode: cleaning up");
  sentence_pub_.reset();
  cloud_sub_.reset();
  buffer_.clear();
  sentence_index_ = 0;
  return CallbackReturn::SUCCESS;
}

CallbackReturn SentenceSegmenterNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "SentenceSegmenterNode: shutting down");
  return CallbackReturn::SUCCESS;
}

CallbackReturn SentenceSegmenterNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "SentenceSegmenterNode: error");
  return CallbackReturn::SUCCESS;
}

void SentenceSegmenterNode::on_cloud_chunk(const buddy_interfaces::msg::CloudChunk &msg) {
  if (current_session_.empty()) {
    current_session_ = msg.session_id;
    sentence_index_ = 0;
  }

  buffer_ += msg.chunk_text;

  // Try to segment on sentence boundaries
  auto sentences = segment(buffer_);
  if (sentences.size() > 1) {
    // All but last are complete sentences
    for (size_t i = 0; i < sentences.size() - 1; ++i) {
      auto s = buddy_interfaces::msg::Sentence();
      s.session_id = current_session_;
      s.text = sentences[i];
      s.index = sentence_index_++;
      sentence_pub_->publish(s);
    }
    buffer_ = sentences.back();
  }

  if (msg.is_final && !buffer_.empty()) {
    flush_buffer(current_session_);
    current_session_.clear();
  }
}

void SentenceSegmenterNode::flush_buffer(const std::string &session_id) {
  if (buffer_.empty()) return;
  auto s = buddy_interfaces::msg::Sentence();
  s.session_id = session_id;
  s.text = buffer_;
  s.index = sentence_index_++;
  sentence_pub_->publish(s);
  buffer_.clear();
}

std::vector<std::string> SentenceSegmenterNode::segment(const std::string &text) {
  std::vector<std::string> result;
  size_t last = 0;
  size_t pos = 0;
  while ((pos = text.find_first_of("。！？.!?", last)) != std::string::npos) {
    pos += 1; // include the punctuation
    result.push_back(text.substr(last, pos - last));
    last = pos;
  }
  if (last < text.size()) {
    result.push_back(text.substr(last));
  }
  if (result.empty()) {
    result.push_back(text);
  }
  return result;
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<SentenceSegmenterNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
```

- [ ] **Step 5: Write unit test with segment logic tests**

`src/buddy_robot/buddy_sentence/test/test_sentence_node.cpp`:
```cpp
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include "buddy_sentence/sentence_segmenter_node.hpp"

class SentenceNodeTest : public ::testing::Test {
protected:
  void SetUp() override {
    node_ = std::make_shared<SentenceSegmenterNode>();
  }
  void TearDown() override { node_.reset(); }
  std::shared_ptr<SentenceSegmenterNode> node_;
};

TEST_F(SentenceNodeTest, NodeName) {
  EXPECT_EQ(node_->get_name(), "sentence");
}

TEST_F(SentenceNodeTest, FullLifecycleSequence) {
  EXPECT_EQ(node_->configure().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->activate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->deactivate().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
  EXPECT_EQ(node_->cleanup().id(),
            lifecycle_msgs::msg::Transition::TRANSITION_CALLBACK_SUCCESS);
}

TEST_F(SentenceNodeTest, SegmentChinese) {
  auto result = node_->segment("你好。我是Buddy！");
  ASSERT_GE(result.size(), 2u);
  EXPECT_EQ(result[0], "你好。");
  EXPECT_EQ(result[1], "我是Buddy！");
}

TEST_F(SentenceNodeTest, SegmentEnglish) {
  auto result = node_->segment("Hello. How are you?");
  ASSERT_GE(result.size(), 2u);
  EXPECT_EQ(result[0], "Hello.");
  EXPECT_EQ(result[1], " How are you?");
}

TEST_F(SentenceNodeTest, SegmentNoPunctuation) {
  auto result = node_->segment("no ending");
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0], "no ending");
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
```

- [ ] **Step 6: Build and test**

```bash
colcon build --packages-select buddy_sentence
colcon test --packages-select buddy_sentence
```

Expected: Build succeeds, all tests pass including segment tests.

- [ ] **Step 7: Commit**

```bash
git add src/buddy_robot/buddy_sentence/
git commit -m "feat(sentence): [PRO-10000] Add sentence segmenter LifecycleNode with tests"
```

---

## Task 11: buddy_bringup (Launch + Params)

**Goal:** Create the bringup package with launch file and parameter configuration.

**Files:**
- Create: `src/buddy_robot/buddy_bringup/package.xml`
- Create: `src/buddy_robot/buddy_bringup/CMakeLists.txt`
- Create: `src/buddy_robot/buddy_bringup/launch/buddy.launch.py`
- Create: `src/buddy_robot/buddy_bringup/params/buddy_params.yaml`

- [ ] **Step 1: Write package.xml**

`src/buddy_robot/buddy_bringup/package.xml`:
```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>buddy_bringup</name>
  <version>0.1.0</version>
  <description>Launch files and parameter configuration for Buddy Robot</description>
  <maintainer email="dev@buddy.robot">seb</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <exec_depend>buddy_audio</exec_depend>
  <exec_depend>buddy_vision</exec_depend>
  <exec_depend>buddy_cloud</exec_depend>
  <exec_depend>buddy_state_machine</exec_depend>
  <exec_depend>buddy_dialog</exec_depend>
  <exec_depend>buddy_sentence</exec_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

- [ ] **Step 2: Write CMakeLists.txt**

`src/buddy_robot/buddy_bringup/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_bringup)

find_package(ament_cmake REQUIRED)

install(DIRECTORY
  launch
  params
  DESTINATION share/${PROJECT_NAME}/
)

ament_package()
```

- [ ] **Step 3: Write launch file**

`src/buddy_robot/buddy_bringup/launch/buddy.launch.py`:
```python
import os
from launch import LaunchDescription
from launch.actions import EmitEvent
from launch_ros.actions import LifecycleNode
from launch_ros.events.lifecycle import ChangeState
from lifecycle_msgs.msg import Transition
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    params_file = os.path.join(
        get_package_share_directory('buddy_bringup'), 'params', 'buddy_params.yaml')

    audio_node = LifecycleNode(
        package='buddy_audio',
        executable='audio_node',
        name='audio',
        parameters=[params_file],
        output='screen',
    )

    vision_node = LifecycleNode(
        package='buddy_vision',
        executable='vision_node',
        name='vision',
        parameters=[params_file],
        output='screen',
    )

    cloud_node = LifecycleNode(
        package='buddy_cloud',
        executable='cloud_node',
        name='cloud',
        parameters=[params_file],
        output='screen',
    )

    state_machine_node = LifecycleNode(
        package='buddy_state_machine',
        executable='state_machine_node',
        name='state_machine',
        parameters=[params_file],
        output='screen',
    )

    dialog_node = LifecycleNode(
        package='buddy_dialog',
        executable='dialog_node',
        name='dialog',
        parameters=[params_file],
        output='screen',
    )

    sentence_node = LifecycleNode(
        package='buddy_sentence',
        executable='sentence_node',
        name='sentence',
        parameters=[params_file],
        output='screen',
    )

    return LaunchDescription([
        audio_node,
        vision_node,
        cloud_node,
        state_machine_node,
        dialog_node,
        sentence_node,
    ])
```

- [ ] **Step 4: Write parameter file**

`src/buddy_robot/buddy_bringup/params/buddy_params.yaml`:
```yaml
audio:
  ros__parameters:
    wake_word_model_path: "/opt/models/wake_word.bin"
    asr_model_path: "/opt/models/sherpa_onnx/"
    sample_rate: 16000
    tts_engine: "edge-tts"
    volume: 0.8

vision:
  ros__parameters:
    model_path: "/opt/models/expression.rknn"
    camera_device: "/dev/video0"
    frame_width: 640
    frame_height: 480
    inference_interval_ms: 500

cloud:
  ros__parameters:
    gateway_url: "wss://gateway.example.com/v1/companion/session"
    reconnect_base_ms: 200
    reconnect_max_ms: 800

dialog:
  ros__parameters:
    max_context_turns: 10
    persona: "buddy"
```

- [ ] **Step 5: Build bringup**

```bash
colcon build --packages-select buddy_bringup
```

Expected: Build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/buddy_robot/buddy_bringup/
git commit -m "feat(bringup): [PRO-10000] Add launch file and parameter configuration"
```

---

## Task 12: Full Build and Integration Test

**Goal:** Build all packages together and verify the full system launches.

- [ ] **Step 1: Full build**

```bash
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install
```

Expected: All 8 packages build successfully (buddy_interfaces + 6 nodes + buddy_bringup).

- [ ] **Step 2: Run all tests**

```bash
colcon test --output-on-failure
colcon test-result --verbose
```

Expected: All tests pass (0 errors, 0 failures).

- [ ] **Step 3: Verify launch**

```bash
source install/setup.bash
ros2 launch buddy_bringup buddy.launch.py
```

Expected: All 6 nodes start, lifecycle transitions visible in logs.

- [ ] **Step 4: Verify node list**

In another terminal:
```bash
source install/setup.bash
ros2 node list
```

Expected:
```
/audio
/vision
/cloud
/state_machine
/dialog
/sentence
```

- [ ] **Step 5: Verify topic list**

```bash
ros2 topic list
```

Expected topics present:
```
/audio/asr_text
/audio/tts_done
/audio/wake_word
/dialog/cloud_response
/dialog/sentence
/dialog/user_input
/display/command
/vision/expression
```

- [ ] **Step 6: Final commit**

```bash
git add -A
git commit -m "feat(build): [PRO-10000] Complete ROS2 migration with full integration"
```

---

## Task Dependency Order

```
Task 1 (branch + cleanup)
  └── Task 2 (filter script)
       └── Task 3 (fetch + build ROS2)
            └── Task 4 (buddy_interfaces) ← all subsequent tasks depend on this
                 ├── Task 5 (buddy_audio)
                 ├── Task 6 (buddy_vision)
                 ├── Task 7 (buddy_cloud)
                 ├── Task 8 (buddy_state_machine)
                 ├── Task 9 (buddy_dialog)
                 └── Task 10 (buddy_sentence)
                      └── Task 11 (buddy_bringup)
                           └── Task 12 (integration test)
```

Tasks 5-10 are independent of each other and can be parallelized.
