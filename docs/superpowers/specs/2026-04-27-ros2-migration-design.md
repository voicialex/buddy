# ROS2 Migration Design

**Date:** 2026-04-27
**Status:** Draft
**Author:** Claude + seb

## 1. Background

Buddy Robot is an edge AI companion robot running on RK3588 (aarch64). The current architecture uses a custom single-process framework (EventBus + IModule + dlopen) with 6 dynamic modules. The migration to ROS2 Jazzy aims to:

- Replace custom framework with ROS2 core (avoid reinventing the wheel)
- Enable multi-process/distributed capabilities
- Leverage ROS2 ecosystem packages
- Deliver source-level with minimal ROS2 source embedded in monorepo

## 2. Decision Summary

| Item | Decision |
|---|---|
| ROS2 Distro | Jazzy Jalisco (LTS, support until 2029) |
| DDS | Fast-DDS (default in Jazzy) |
| Source Organization | System-installed ROS2 Jazzy (`/opt/ros/jazzy`) + project source in monorepo |
| Build System | colcon |
| Module Pattern | rclcpp_lifecycle LifecycleNode + rclcpp_components |
| Build Target | x86 development + aarch64 deployment (native build) |
| Migration Scope | Complete rewrite of all 6 modules (using system-installed ROS2 Jazzy) |

## 3. ROS2 Package Dependencies

> **Note:** The project uses the system-installed ROS2 Jazzy (`/opt/ros/jazzy`) instead of source-building minimal packages. The package list below documents the required capabilities; all are available in the Jazzy apt packages.

### Required Capabilities (from Jazzy apt)
```
ament_cmake_core, ament_cmake_export_dependencies, ament_cmake_export_include_directories,
ament_cmake_export_interfaces, ament_cmake_export_libraries, ament_cmake_export_targets,
ament_cmake_include_directories, ament_cmake_libraries, ament_cmake_python,
ament_cmake_target_dependencies, ament_cmake_test, ament_cmake_version,
ament_package, ament_index_cpp, ament_cmake_gen_version_h
```

### Message Generation Layer (rosidl)
```
rosidl_adapter, rosidl_cmake, rosidl_generator_c, rosidl_generator_cpp,
rosidl_parser, rosidl_runtime_c, rosidl_runtime_cpp,
rosidl_typesupport_interface, rosidl_typesupport_c, rosidl_typesupport_cpp,
rosidl_typesupport_fastrtps_c, rosidl_typesupport_fastrtps_cpp,
rosidl_default_generators, rosidl_default_runtime,
fastrtps, foonathan_memory_vendor
```

### Interface Layer (messages)
```
builtin_interfaces, std_msgs, sensor_msgs, geometry_msgs,
lifecycle_msgs, composition_interfaces, service_msgs
```

### Middleware Layer (rmw)
```
rcutils, rcl, rcl_yaml_param_parser, rmw, rmw_fastrtps_shared_cpp,
rmw_fastrtps_cpp, rmw_fastrtps_dynamic_cpp, rmw_implementation,
tracetools, osrf_pycommon
```

### C++ Client Layer
```
rclcpp, rclcpp_lifecycle, rclcpp_components, class_loader,
pluginlib, rcpputils
```

### Image Layer
```
image_transport, image_common, cv_bridge, vision_opencv,
camera_calibration_parsers
```

## 4. Repository Structure

```
buddy_robot/
├── third_party/
│   └── ros2/
│       └── jazzy/
│           └── ros2_minimal.repos      # Filtered repos file (for reference)
├── src/
│   └── buddy_robot/
│       ├── buddy_interfaces/          # Custom messages & services
│       │   ├── msg/
│       │   │   ├── UserInput.msg
│       │   │   ├── CloudChunk.msg
│       │   │   ├── Sentence.msg
│       │   │   ├── ExpressionResult.msg
│       │   │   └── DisplayCommand.msg
│       │   ├── srv/
│       │   │   └── CaptureImage.srv
│       │   ├── CMakeLists.txt
│       │   └── package.xml
│       ├── buddy_audio/               # ASR/Wake/TTS (LifecycleNode)
│       ├── buddy_vision/              # Expression recognition model (LifecycleNode)
│       ├── buddy_cloud/               # WebSocket cloud client (LifecycleNode)
│       ├── buddy_state_machine/       # State orchestrator (LifecycleNode)
│       ├── buddy_dialog/              # Dialog manager (LifecycleNode)
│       ├── buddy_sentence/            # Sentence segmenter (LifecycleNode)
│       └── buddy_bringup/             # Launch files & params
│           ├── launch/
│           │   └── buddy.launch.py
│           ├── params/
│           │   └── buddy_params.yaml
│           ├── CMakeLists.txt
│           └── package.xml
├── config/
├── docs/
├── test/
├── scripts/
│   └── filter_ros2_repos.py
└── README.md
```

## 5. Topic & Service Design

### Audio Chain
| Topic | Type | Description |
|---|---|---|
| `/audio/wake_word` | `std_msgs/msg/String` | Wake word detected |
| `/audio/asr_text` | `std_msgs/msg/String` | ASR recognition result |
| `/audio/tts_done` | `std_msgs/msg/Empty` | TTS playback complete |

### Vision Chain
| Topic/Service | Type | Description |
|---|---|---|
| `/vision/expression` | `buddy_interfaces/msg/ExpressionResult` | Expression model inference result |
| `/vision/capture` | `buddy_interfaces/srv/CaptureImage` | Request capture (triggers model inference) |

### Dialog Chain
| Topic | Type | Description |
|---|---|---|
| `/dialog/user_input` | `buddy_interfaces/msg/UserInput` | User input (text + context) |
| `/dialog/cloud_response` | `buddy_interfaces/msg/CloudChunk` | Cloud streaming response chunk |
| `/dialog/sentence` | `buddy_interfaces/msg/Sentence` | Segmented sentence for TTS |

### System Control
| Topic | Type | Description |
|---|---|---|
| `/system/state` | `lifecycle_msgs/msg/TransitionEvent` | State machine transition event |
| `/display/command` | `buddy_interfaces/msg/DisplayCommand` | Display instruction |

### Custom Message Definitions

```
# buddy_interfaces/msg/UserInput.msg
string text
string session_id
builtin_interfaces/Time timestamp
```

```
# buddy_interfaces/msg/CloudChunk.msg
string session_id
string chunk_text
bool is_final
```

```
# buddy_interfaces/msg/Sentence.msg
string session_id
string text
uint32 index
```

```
# buddy_interfaces/msg/ExpressionResult.msg
string expression        # happy, sad, neutral, angry, surprised
float32 confidence
builtin_interfaces/Time timestamp
```

```
# buddy_interfaces/msg/DisplayCommand.msg
string command           # show_text, show_emoji, show_image
string payload
```

```
# buddy_interfaces/srv/CaptureImage.srv
---
sensor_msgs/msg/Image image
```

## 6. Module → LifecycleNode Mapping

Each module becomes a `rclcpp_lifecycle::LifecycleNode`:

| Current IModule Callback | ROS2 Lifecycle Transition |
|---|---|
| `OnLoad` | Constructor + `on_configure` |
| `OnInit` | `on_configure` |
| `OnStart` | `on_activate` |
| Running | `active` state |
| `OnStop` | `on_deactivate` |
| `OnDeinit` | `on_cleanup` |
| `OnUnload` | Destructor |
| Error | `on_error` (log error, release resources) |
| Process shutdown | `on_shutdown` (save state, cleanup) |

Dynamic loading via `RCLCPP_COMPONENTS_REGISTER_NODE` macro replaces `CreateModule`/`DestroyModule` factory functions.

## 7. Data Flow

```
User speaks → Wake detection → ASR → State machine → Dialog manager → Cloud LLM → Sentence segmentation → TTS → Playback
User enters frame → Expression recognition (model) → State machine → Interaction strategy
```

```
┌─────────────────┐  /audio/wake_word  ┌──────────────────┐
│  buddy_audio    │───────────────────→│ buddy_state_machine│
│  (ASR/Wake/TTS) │  /audio/asr_text   │  (State dispatch)  │
│                 │───────────────────→│                    │
│                 │←─ /audio/tts_done ─│                    │
└─────────────────┘                    └────────┬───────────┘
                                                │ /dialog/user_input
                                                ↓
┌─────────────────┐  /dialog/cloud_response  ┌──────────────────┐
│  buddy_cloud    │←────────────────────────│  buddy_dialog     │
│  (WebSocket)    │                           │  (Dialog manager) │
└─────────────────┘                           └────────┬──────────┘
                                                       │ /dialog/sentence
                                                       ↓
                                              ┌──────────────────┐
                                              │ buddy_sentence    │
                                              │ (Segment → TTS)   │
                                              └──────────────────┘

┌─────────────────┐  /vision/expression  ┌──────────────────┐
│  buddy_vision   │────────────────────→│ buddy_state_machine│
│  (Expression    │←── CaptureImage ────│  (Trigger recogn.) │
│   model)        │  (Service)           └──────────────────┘
└─────────────────┘
```

## 8. Build & Deploy

### Install ROS2 Jazzy (if not already installed)
```bash
sudo apt install ros-jazzy-desktop
# Or minimal: ros-jazzy-ros-base ros-jazzy-rclcpp-lifecycle ros-jazzy-rclcpp-components ros-jazzy-image-transport
```

### Build buddy modules
```bash
source /opt/ros/jazzy/setup.bash
colcon build --cmake-args -DCMAKE_BUILD_TYPE=Release
```

### Deploy on RK3588
Install ROS2 Jazzy aarch64 packages on target board, then same `colcon build`.

## 9. Launch Configuration

```python
# buddy_bringup/launch/buddy.launch.py
def generate_launch_description():
    params_file = os.path.join(
        get_package_share_directory('buddy_bringup'), 'params', 'buddy_params.yaml')

    return launch.LaunchDescription([
        LifecycleNode(package='buddy_audio', executable='audio_node',
                      name='audio', parameters=[params_file]),
        LifecycleNode(package='buddy_vision', executable='vision_node',
                      name='vision', parameters=[params_file]),
        LifecycleNode(package='buddy_state_machine', executable='state_machine_node',
                      name='state_machine', parameters=[params_file]),
        LifecycleNode(package='buddy_dialog', executable='dialog_node',
                      name='dialog', parameters=[params_file]),
        LifecycleNode(package='buddy_sentence', executable='sentence_node',
                      name='sentence', parameters=[params_file]),
        LifecycleNode(package='buddy_cloud', executable='cloud_node',
                      name='cloud', parameters=[params_file]),
    ])
```

## 10. Parameter Configuration (YAML)

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

## 11. Testing Strategy

| Layer | Tool | Scope |
|---|---|---|
| Unit test | gtest + ament_add_gtest | Internal logic per node |
| Integration test | launch_testing | Cross-node Topic communication |
| Message test | rosidl auto-generation | Custom message compilation |

Test structure per package:
```
buddy_audio/
├── test/
│   ├── test_audio_node.cpp       # gtest: node creation, param loading
│   └── test_asr_integration.py   # launch_testing: send audio, verify Topic
├── src/
├── include/
├── CMakeLists.txt
└── package.xml
```
