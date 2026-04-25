# Intra-Process Component Migration Design

## Goal

Migrate buddy's 6 LifecycleNode executables into a single-process `rclcpp_components` architecture with intra-process communication enabled, add per-node parameter files, and verify intra-process zero-copy with a demo.

## Current State

- 6 LifecycleNodes: audio, vision, cloud, state_machine, dialog, sentence
- Each compiled as independent executable with its own `main()`
- Communication via DDS (inter-process)
- Single shared `buddy_params.yaml` (only audio/vision/cloud/dialog have params)
- Launch file uses `LifecycleNode` actions

## Target State

- All 6 nodes registered as `rclcpp_components` plugins (shared libraries)
- Loaded into one `ComposableNodeContainer` with `use_intra_process_comms: true`
- Per-node yaml parameter files
- Demo logging to verify intra-process pointer sharing
- Standalone executables preserved for debugging

## Architecture

```
┌──────────────── Single Process ────────────────┐
│  ComposableNodeContainer                        │
│                                                 │
│  audio ──→ state_machine ──→ cloud              │
│    ↑            ↓                               │
│  sentence    dialog         vision              │
│                                                 │
│  All pub/sub: intra-process (zero-copy)         │
└─────────────────────────────────────────────────┘
```

## Changes Per Node Package

For each of the 6 packages (buddy_audio, buddy_vision, buddy_cloud, buddy_state_machine, buddy_dialog, buddy_sentence):

### 1. Source: Register as Component

Add to end of main .cpp file (outside `#ifndef BUDDY_UNIT_TEST` guard):

```cpp
#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(AudioPipelineNode)  // class name varies
```

### 2. CMakeLists.txt: Shared Library + Executable

```cmake
find_package(rclcpp_components REQUIRED)

# Shared library (for component loading)
add_library(${NODE_NAME}_component SHARED src/${NODE_NAME}.cpp)
ament_target_dependencies(${NODE_NAME}_component rclcpp rclcpp_lifecycle rclcpp_components ...)
rclcpp_components_register_nodes(${NODE_NAME}_component "ClassName")

# Standalone executable (for debugging)
add_executable(${NODE_NAME} src/${NODE_NAME}.cpp)
ament_target_dependencies(${NODE_NAME} rclcpp rclcpp_lifecycle ...)
```

### 3. package.xml: Add Dependency

```xml
<depend>rclcpp_components</depend>
```

## Parameter Files

Split `buddy_params.yaml` into per-node files under `buddy_bringup/params/`:

| File | Content |
|------|---------|
| `audio.yaml` | wake_word_model_path, asr_model_path, sample_rate, tts_engine, volume |
| `vision.yaml` | model_path, camera_device, frame_width, frame_height, inference_interval_ms |
| `cloud.yaml` | gateway_url, reconnect_base_ms, reconnect_max_ms |
| `dialog.yaml` | max_context_turns, persona |
| `state_machine.yaml` | idle_timeout_s, wake_word_enabled |
| `sentence.yaml` | max_sentence_length, delimiter_chars |

Each file follows the standard ROS2 parameter format:
```yaml
<node_name>:
  ros__parameters:
    key: value
```

## Launch File

Replace `buddy.launch.py` with `ComposableNodeContainer`:

```python
from launch import LaunchDescription
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
from ament_index_python.packages import get_package_share_directory
import os

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

## Demo: Verifying Intra-Process Communication

Add temporary debug logging on the `audio → state_machine` path (wake_word topic):

**AudioPipelineNode (publisher side):**
```cpp
auto msg = std::make_unique<std_msgs::msg::String>();
msg->data = "wake";
RCLCPP_INFO(get_logger(), "[INTRA-DEMO] pub ptr: %p", (void*)msg.get());
wake_word_pub_->publish(std::move(msg));
```

**StateMachineNode (subscriber side):**
```cpp
void on_wake_word(const std_msgs::msg::String &msg) {
  RCLCPP_INFO(get_logger(), "[INTRA-DEMO] sub ptr: %p", (void*)&msg);
  // ... existing logic
}
```

**Expected result:** Both log lines print the same pointer address, confirming zero-copy intra-process delivery.

## Key Constraints

1. **unique_ptr publishing required** for true zero-copy. Existing code using `shared_ptr` or stack messages must switch to `std::make_unique<MsgType>()` + `publish(std::move(msg))`.
2. **LifecycleNode compatibility**: `rclcpp_lifecycle::LifecycleNode` is supported as a component in ROS2 Humble.
3. **Tests unaffected**: Unit tests create nodes directly, not via container. The `#ifndef BUDDY_UNIT_TEST` guard stays for standalone `main()`.
4. **ros2_core dependency**: Ensure `rclcpp_components` is included in the ros2_core build targets.

## File Changes Summary

| Package | Files Modified | Files Added |
|---------|---------------|-------------|
| buddy_audio | CMakeLists.txt, package.xml, audio_pipeline_node.cpp | — |
| buddy_vision | CMakeLists.txt, package.xml, vision_pipeline_node.cpp | — |
| buddy_cloud | CMakeLists.txt, package.xml, cloud_client_node.cpp | — |
| buddy_state_machine | CMakeLists.txt, package.xml, state_machine_node.cpp | — |
| buddy_dialog | CMakeLists.txt, package.xml, dialog_manager_node.cpp | — |
| buddy_sentence | CMakeLists.txt, package.xml, sentence_segmenter_node.cpp | — |
| buddy_bringup | CMakeLists.txt, buddy.launch.py | params/{audio,vision,cloud,state_machine,dialog,sentence}.yaml |

## Risks

- If any node does blocking work in callbacks, it blocks all nodes in the single-threaded container. Mitigation: use `MultiThreadedExecutor` variant if needed later.
- Service calls (CaptureImage in state_machine) within intra-process need careful handling to avoid deadlocks in single-threaded executor.
