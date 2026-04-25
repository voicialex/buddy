# Intra-Process Component Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert 6 LifecycleNode executables into rclcpp_components shared libraries loaded in a single ComposableNodeContainer with intra-process communication, per-node yaml configs, and a demo proving zero-copy.

**Architecture:** Each node registers via `RCLCPP_COMPONENTS_REGISTER_NODE`, compiled as both shared library (for container loading) and standalone executable (for debugging). A new launch file uses `ComposableNodeContainer` with `use_intra_process_comms: True`. The audio→state_machine path adds pointer-address logging to prove intra-process delivery.

**Tech Stack:** ROS2 Humble, rclcpp_components, rclcpp_lifecycle, colcon/CMake, Python launch

---

### Task 1: Convert buddy_audio to Component

**Files:**
- Modify: `src/buddy_robot/buddy_audio/package.xml`
- Modify: `src/buddy_robot/buddy_audio/CMakeLists.txt`
- Modify: `src/buddy_robot/buddy_audio/src/audio_pipeline_node.cpp`

- [ ] **Step 1: Add rclcpp_components dependency to package.xml**

Add `<depend>rclcpp_components</depend>` after the existing `rclcpp_lifecycle` depend:

```xml
  <depend>rclcpp_lifecycle</depend>
  <depend>rclcpp_components</depend>
  <depend>std_msgs</depend>
```

- [ ] **Step 2: Add component registration macro to source**

Add at the very end of `src/audio_pipeline_node.cpp` (after the `#endif` of BUDDY_UNIT_TEST):

```cpp
#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(AudioPipelineNode)
```

- [ ] **Step 3: Rewrite CMakeLists.txt to build shared lib + executable**

Replace the entire `CMakeLists.txt` with:

```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_audio)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(std_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)

# Shared library for component loading
add_library(audio_component SHARED src/audio_pipeline_node.cpp)
target_include_directories(audio_component PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(audio_component PUBLIC cxx_std_17)
target_compile_definitions(audio_component PRIVATE BUDDY_UNIT_TEST)
ament_target_dependencies(audio_component rclcpp rclcpp_lifecycle rclcpp_components std_msgs buddy_interfaces)
rclcpp_components_register_nodes(audio_component "AudioPipelineNode")
install(TARGETS audio_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

# Standalone executable for debugging
add_executable(audio_node src/audio_pipeline_node.cpp)
target_include_directories(audio_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(audio_node PUBLIC cxx_std_17)
ament_target_dependencies(audio_node rclcpp rclcpp_lifecycle rclcpp_components std_msgs buddy_interfaces)
install(TARGETS audio_node DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_audio_node test/test_audio_node.cpp src/audio_pipeline_node.cpp)
  target_include_directories(test_audio_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_audio_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_audio_node rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
  target_compile_definitions(test_audio_node PRIVATE BUDDY_UNIT_TEST)
endif()
ament_package()
```

Note: The shared library uses `BUDDY_UNIT_TEST` define to suppress `main()` — the registration macro provides the entry point for the component container.

- [ ] **Step 4: Verify build compiles**

Run: `cd /home/seb/workspace/software/buddy && colcon build --packages-select buddy_audio`
Expected: SUCCESS, produces `libaudio_component.so` in install/buddy_audio/lib/

- [ ] **Step 5: Commit**

```bash
git add src/buddy_robot/buddy_audio/
git commit -m "feat(module): [PRO-10000] Convert buddy_audio to rclcpp_components"
```

---

### Task 2: Convert buddy_vision to Component

**Files:**
- Modify: `src/buddy_robot/buddy_vision/package.xml`
- Modify: `src/buddy_robot/buddy_vision/CMakeLists.txt`
- Modify: `src/buddy_robot/buddy_vision/src/vision_pipeline_node.cpp`

- [ ] **Step 1: Add rclcpp_components dependency to package.xml**

Add `<depend>rclcpp_components</depend>` after `rclcpp_lifecycle`:

```xml
  <depend>rclcpp_lifecycle</depend>
  <depend>rclcpp_components</depend>
```

- [ ] **Step 2: Add component registration macro to source**

Add at end of `src/vision_pipeline_node.cpp` (after `#endif`):

```cpp
#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(VisionPipelineNode)
```

- [ ] **Step 3: Rewrite CMakeLists.txt**

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

add_library(vision_component SHARED src/vision_pipeline_node.cpp)
target_include_directories(vision_component PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(vision_component PUBLIC cxx_std_17)
target_compile_definitions(vision_component PRIVATE BUDDY_UNIT_TEST)
ament_target_dependencies(vision_component rclcpp rclcpp_lifecycle rclcpp_components sensor_msgs buddy_interfaces)
rclcpp_components_register_nodes(vision_component "VisionPipelineNode")
install(TARGETS vision_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

add_executable(vision_node src/vision_pipeline_node.cpp)
target_include_directories(vision_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(vision_node PUBLIC cxx_std_17)
ament_target_dependencies(vision_node rclcpp rclcpp_lifecycle rclcpp_components sensor_msgs buddy_interfaces)
install(TARGETS vision_node DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_vision_node test/test_vision_node.cpp src/vision_pipeline_node.cpp)
  target_include_directories(test_vision_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_vision_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_vision_node rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
  target_compile_definitions(test_vision_node PRIVATE BUDDY_UNIT_TEST)
endif()
ament_package()
```

- [ ] **Step 4: Verify build**

Run: `colcon build --packages-select buddy_vision`
Expected: SUCCESS

- [ ] **Step 5: Commit**

```bash
git add src/buddy_robot/buddy_vision/
git commit -m "feat(module): [PRO-10000] Convert buddy_vision to rclcpp_components"
```

---

### Task 3: Convert buddy_cloud to Component

**Files:**
- Modify: `src/buddy_robot/buddy_cloud/package.xml`
- Modify: `src/buddy_robot/buddy_cloud/CMakeLists.txt`
- Modify: `src/buddy_robot/buddy_cloud/src/cloud_client_node.cpp`

- [ ] **Step 1: Add rclcpp_components dependency to package.xml**

```xml
  <depend>rclcpp_lifecycle</depend>
  <depend>rclcpp_components</depend>
```

- [ ] **Step 2: Add component registration macro**

End of `src/cloud_client_node.cpp` (after `#endif`):

```cpp
#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(CloudClientNode)
```

- [ ] **Step 3: Rewrite CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_cloud)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_library(cloud_component SHARED src/cloud_client_node.cpp)
target_include_directories(cloud_component PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(cloud_component PUBLIC cxx_std_17)
target_compile_definitions(cloud_component PRIVATE BUDDY_UNIT_TEST)
ament_target_dependencies(cloud_component rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
rclcpp_components_register_nodes(cloud_component "CloudClientNode")
install(TARGETS cloud_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

add_executable(cloud_node src/cloud_client_node.cpp)
target_include_directories(cloud_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(cloud_node PUBLIC cxx_std_17)
ament_target_dependencies(cloud_node rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
install(TARGETS cloud_node DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_cloud_node test/test_cloud_node.cpp src/cloud_client_node.cpp)
  target_include_directories(test_cloud_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_cloud_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_cloud_node rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
  target_compile_definitions(test_cloud_node PRIVATE BUDDY_UNIT_TEST)
endif()
ament_package()
```

- [ ] **Step 4: Verify build**

Run: `colcon build --packages-select buddy_cloud`
Expected: SUCCESS

- [ ] **Step 5: Commit**

```bash
git add src/buddy_robot/buddy_cloud/
git commit -m "feat(module): [PRO-10000] Convert buddy_cloud to rclcpp_components"
```

---

### Task 4: Convert buddy_state_machine to Component

**Files:**
- Modify: `src/buddy_robot/buddy_state_machine/package.xml`
- Modify: `src/buddy_robot/buddy_state_machine/CMakeLists.txt`
- Modify: `src/buddy_robot/buddy_state_machine/src/state_machine_node.cpp`

- [ ] **Step 1: Add rclcpp_components dependency to package.xml**

```xml
  <depend>rclcpp_lifecycle</depend>
  <depend>rclcpp_components</depend>
```

- [ ] **Step 2: Add component registration macro**

End of `src/state_machine_node.cpp` (after `#endif`):

```cpp
#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(StateMachineNode)
```

- [ ] **Step 3: Rewrite CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_state_machine)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(std_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_library(state_machine_component SHARED src/state_machine_node.cpp)
target_include_directories(state_machine_component PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(state_machine_component PUBLIC cxx_std_17)
target_compile_definitions(state_machine_component PRIVATE BUDDY_UNIT_TEST)
ament_target_dependencies(state_machine_component rclcpp rclcpp_lifecycle rclcpp_components std_msgs buddy_interfaces)
rclcpp_components_register_nodes(state_machine_component "StateMachineNode")
install(TARGETS state_machine_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

add_executable(state_machine_node src/state_machine_node.cpp)
target_include_directories(state_machine_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(state_machine_node PUBLIC cxx_std_17)
ament_target_dependencies(state_machine_node rclcpp rclcpp_lifecycle rclcpp_components std_msgs buddy_interfaces)
install(TARGETS state_machine_node DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_state_machine_node test/test_state_machine_node.cpp src/state_machine_node.cpp)
  target_include_directories(test_state_machine_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_state_machine_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_state_machine_node rclcpp rclcpp_lifecycle rclcpp_components std_msgs buddy_interfaces)
  target_compile_definitions(test_state_machine_node PRIVATE BUDDY_UNIT_TEST)
endif()
ament_package()
```

- [ ] **Step 4: Verify build**

Run: `colcon build --packages-select buddy_state_machine`
Expected: SUCCESS

- [ ] **Step 5: Commit**

```bash
git add src/buddy_robot/buddy_state_machine/
git commit -m "feat(module): [PRO-10000] Convert buddy_state_machine to rclcpp_components"
```

---

### Task 5: Convert buddy_dialog to Component

**Files:**
- Modify: `src/buddy_robot/buddy_dialog/package.xml`
- Modify: `src/buddy_robot/buddy_dialog/CMakeLists.txt`
- Modify: `src/buddy_robot/buddy_dialog/src/dialog_manager_node.cpp`

- [ ] **Step 1: Add rclcpp_components dependency to package.xml**

```xml
  <depend>rclcpp_lifecycle</depend>
  <depend>rclcpp_components</depend>
```

- [ ] **Step 2: Add component registration macro**

End of `src/dialog_manager_node.cpp` (after `#endif`):

```cpp
#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(DialogManagerNode)
```

- [ ] **Step 3: Rewrite CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_dialog)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_library(dialog_component SHARED src/dialog_manager_node.cpp)
target_include_directories(dialog_component PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(dialog_component PUBLIC cxx_std_17)
target_compile_definitions(dialog_component PRIVATE BUDDY_UNIT_TEST)
ament_target_dependencies(dialog_component rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
rclcpp_components_register_nodes(dialog_component "DialogManagerNode")
install(TARGETS dialog_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

add_executable(dialog_node src/dialog_manager_node.cpp)
target_include_directories(dialog_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(dialog_node PUBLIC cxx_std_17)
ament_target_dependencies(dialog_node rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
install(TARGETS dialog_node DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_dialog_node test/test_dialog_node.cpp src/dialog_manager_node.cpp)
  target_include_directories(test_dialog_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_dialog_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_dialog_node rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
  target_compile_definitions(test_dialog_node PRIVATE BUDDY_UNIT_TEST)
endif()
ament_package()
```

- [ ] **Step 4: Verify build**

Run: `colcon build --packages-select buddy_dialog`
Expected: SUCCESS

- [ ] **Step 5: Commit**

```bash
git add src/buddy_robot/buddy_dialog/
git commit -m "feat(module): [PRO-10000] Convert buddy_dialog to rclcpp_components"
```

---

### Task 6: Convert buddy_sentence to Component

**Files:**
- Modify: `src/buddy_robot/buddy_sentence/package.xml`
- Modify: `src/buddy_robot/buddy_sentence/CMakeLists.txt`
- Modify: `src/buddy_robot/buddy_sentence/src/sentence_segmenter_node.cpp`

- [ ] **Step 1: Add rclcpp_components dependency to package.xml**

```xml
  <depend>rclcpp_lifecycle</depend>
  <depend>rclcpp_components</depend>
```

- [ ] **Step 2: Add component registration macro**

End of `src/sentence_segmenter_node.cpp` (after `#endif`):

```cpp
#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(SentenceSegmenterNode)
```

- [ ] **Step 3: Rewrite CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.8)
project(buddy_sentence)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_library(sentence_component SHARED src/sentence_segmenter_node.cpp)
target_include_directories(sentence_component PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(sentence_component PUBLIC cxx_std_17)
target_compile_definitions(sentence_component PRIVATE BUDDY_UNIT_TEST)
ament_target_dependencies(sentence_component rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
rclcpp_components_register_nodes(sentence_component "SentenceSegmenterNode")
install(TARGETS sentence_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

add_executable(sentence_node src/sentence_segmenter_node.cpp)
target_include_directories(sentence_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(sentence_node PUBLIC cxx_std_17)
ament_target_dependencies(sentence_node rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
install(TARGETS sentence_node DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_sentence_node test/test_sentence_node.cpp src/sentence_segmenter_node.cpp)
  target_include_directories(test_sentence_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_sentence_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_sentence_node rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
  target_compile_definitions(test_sentence_node PRIVATE BUDDY_UNIT_TEST)
endif()
ament_package()
```

- [ ] **Step 4: Verify build**

Run: `colcon build --packages-select buddy_sentence`
Expected: SUCCESS

- [ ] **Step 5: Commit**

```bash
git add src/buddy_robot/buddy_sentence/
git commit -m "feat(module): [PRO-10000] Convert buddy_sentence to rclcpp_components"
```

---

### Task 7: Create Per-Node Parameter Files

**Files:**
- Create: `src/buddy_robot/buddy_bringup/params/audio.yaml`
- Create: `src/buddy_robot/buddy_bringup/params/vision.yaml`
- Create: `src/buddy_robot/buddy_bringup/params/cloud.yaml`
- Create: `src/buddy_robot/buddy_bringup/params/state_machine.yaml`
- Create: `src/buddy_robot/buddy_bringup/params/dialog.yaml`
- Create: `src/buddy_robot/buddy_bringup/params/sentence.yaml`
- Delete: `src/buddy_robot/buddy_bringup/params/buddy_params.yaml`

- [ ] **Step 1: Create audio.yaml**

```yaml
audio:
  ros__parameters:
    wake_word_model_path: "/opt/models/wake_word.bin"
    asr_model_path: "/opt/models/sherpa_onnx/"
    sample_rate: 16000
    tts_engine: "edge-tts"
    volume: 0.8
```

- [ ] **Step 2: Create vision.yaml**

```yaml
vision:
  ros__parameters:
    model_path: "/opt/models/expression.rknn"
    camera_device: "/dev/video0"
    frame_width: 640
    frame_height: 480
    inference_interval_ms: 500
```

- [ ] **Step 3: Create cloud.yaml**

```yaml
cloud:
  ros__parameters:
    gateway_url: "wss://gateway.example.com/v1/companion/session"
    reconnect_base_ms: 200
    reconnect_max_ms: 800
```

- [ ] **Step 4: Create state_machine.yaml**

```yaml
state_machine:
  ros__parameters:
    idle_timeout_s: 30.0
    wake_word_enabled: true
```

- [ ] **Step 5: Create dialog.yaml**

```yaml
dialog:
  ros__parameters:
    max_context_turns: 10
    persona: "buddy"
```

- [ ] **Step 6: Create sentence.yaml**

```yaml
sentence:
  ros__parameters:
    max_sentence_length: 200
    delimiter_chars: ".!?。！？"
```

- [ ] **Step 7: Remove old buddy_params.yaml**

```bash
rm src/buddy_robot/buddy_bringup/params/buddy_params.yaml
```

- [ ] **Step 8: Commit**

```bash
git add src/buddy_robot/buddy_bringup/params/
git commit -m "feat(module): [PRO-10000] Split params into per-node yaml files"
```

---

### Task 8: Rewrite Launch File for ComposableNodeContainer

**Files:**
- Modify: `src/buddy_robot/buddy_bringup/launch/buddy.launch.py`

- [ ] **Step 1: Replace launch file content**

```python
import os
from launch import LaunchDescription
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    params_dir = os.path.join(
        get_package_share_directory('buddy_bringup'), 'params')

    def params(name):
        return [os.path.join(params_dir, f'{name}.yaml')]

    container = ComposableNodeContainer(
        name='buddy_container',
        namespace='',
        package='rclcpp_components',
        executable='component_container',
        composable_node_descriptions=[
            ComposableNode(
                package='buddy_audio',
                plugin='AudioPipelineNode',
                name='audio',
                parameters=params('audio'),
                extra_arguments=[{'use_intra_process_comms': True}],
            ),
            ComposableNode(
                package='buddy_vision',
                plugin='VisionPipelineNode',
                name='vision',
                parameters=params('vision'),
                extra_arguments=[{'use_intra_process_comms': True}],
            ),
            ComposableNode(
                package='buddy_cloud',
                plugin='CloudClientNode',
                name='cloud',
                parameters=params('cloud'),
                extra_arguments=[{'use_intra_process_comms': True}],
            ),
            ComposableNode(
                package='buddy_state_machine',
                plugin='StateMachineNode',
                name='state_machine',
                parameters=params('state_machine'),
                extra_arguments=[{'use_intra_process_comms': True}],
            ),
            ComposableNode(
                package='buddy_dialog',
                plugin='DialogManagerNode',
                name='dialog',
                parameters=params('dialog'),
                extra_arguments=[{'use_intra_process_comms': True}],
            ),
            ComposableNode(
                package='buddy_sentence',
                plugin='SentenceSegmenterNode',
                name='sentence',
                parameters=params('sentence'),
                extra_arguments=[{'use_intra_process_comms': True}],
            ),
        ],
        output='screen',
    )

    return LaunchDescription([container])
```

- [ ] **Step 2: Verify launch file syntax**

Run: `python3 -c "import importlib.util; spec = importlib.util.spec_from_file_location('m', 'src/buddy_robot/buddy_bringup/launch/buddy.launch.py'); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); print('OK')"`
Expected: `OK` (no syntax errors)

- [ ] **Step 3: Commit**

```bash
git add src/buddy_robot/buddy_bringup/launch/buddy.launch.py
git commit -m "feat(module): [PRO-10000] Use ComposableNodeContainer with intra-process comms"
```

---

### Task 9: Add Intra-Process Demo Logging

**Files:**
- Modify: `src/buddy_robot/buddy_audio/src/audio_pipeline_node.cpp`
- Modify: `src/buddy_robot/buddy_state_machine/src/state_machine_node.cpp`

- [ ] **Step 1: Modify audio node to publish with unique_ptr and log pointer**

In `audio_pipeline_node.cpp`, replace the `on_configure` publisher creation for wake_word to use the existing publisher but change how we publish in a new demo method. Actually, the simplest demo is to modify `on_activate` to publish a test wake word:

Add a method at the bottom (before `#ifndef BUDDY_UNIT_TEST`):

First, change `on_activate` to send a demo wake word with pointer logging:

```cpp
CallbackReturn AudioPipelineNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: activating");
  // INTRA-DEMO: publish with unique_ptr for zero-copy
  auto msg = std::make_unique<std_msgs::msg::String>();
  msg->data = "hey_buddy";
  RCLCPP_INFO(get_logger(), "[INTRA-DEMO] audio pub ptr: %p", (void*)msg.get());
  wake_word_pub_->publish(std::move(msg));
  return CallbackReturn::SUCCESS;
}
```

- [ ] **Step 2: Modify state_machine node to log received pointer**

In `state_machine_node.cpp`, change `on_wake_word` to log the pointer:

```cpp
void StateMachineNode::on_wake_word(const std_msgs::msg::String &msg) {
  RCLCPP_INFO(get_logger(), "[INTRA-DEMO] state_machine sub ptr: %p", (void*)&msg);
  RCLCPP_INFO(get_logger(), "Wake word detected");
  transition(State::LISTENING);
}
```

- [ ] **Step 3: Rebuild both packages**

Run: `colcon build --packages-select buddy_audio buddy_state_machine`
Expected: SUCCESS

- [ ] **Step 4: Commit**

```bash
git add src/buddy_robot/buddy_audio/src/audio_pipeline_node.cpp src/buddy_robot/buddy_state_machine/src/state_machine_node.cpp
git commit -m "feat(module): [PRO-10000] Add intra-process demo logging on audio->state_machine path"
```

---

### Task 10: Full Build and Integration Verification

**Files:** None (verification only)

- [ ] **Step 1: Clean build all buddy packages**

Run: `colcon build --packages-select buddy_interfaces buddy_audio buddy_vision buddy_cloud buddy_state_machine buddy_dialog buddy_sentence buddy_bringup`
Expected: All packages build successfully

- [ ] **Step 2: Verify component libraries exist**

Run: `find install/ -name "*_component.so" | sort`
Expected output:
```
install/buddy_audio/lib/libaudio_component.so
install/buddy_cloud/lib/libcloud_component.so
install/buddy_dialog/lib/libdialog_component.so
install/buddy_sentence/lib/libsentence_component.so
install/buddy_state_machine/lib/libstate_machine_component.so
install/buddy_vision/lib/libvision_component.so
```

- [ ] **Step 3: Verify component registration is discoverable**

Run: `source install/setup.bash && ros2 component types 2>/dev/null | grep -i buddy || echo "Need ROS2 runtime for full check"`

If ROS2 runtime available, expected output includes:
```
buddy_audio
  AudioPipelineNode
buddy_vision
  VisionPipelineNode
...
```

- [ ] **Step 4: Run existing unit tests**

Run: `colcon test --packages-select buddy_audio buddy_vision buddy_cloud buddy_state_machine buddy_dialog buddy_sentence && colcon test-result --verbose`
Expected: All tests pass (component registration doesn't affect unit tests since they use BUDDY_UNIT_TEST define)

- [ ] **Step 5: Commit final verification (if any fixes needed)**

Only commit if fixes were required during verification.
