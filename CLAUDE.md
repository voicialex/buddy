# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

Prerequisite: extract pre-built dependencies into `prebuilt/`:

```bash
# ROS 2 core
mkdir -p prebuilt/ros2_core
tar xzf prebuilt/ros2-humble-x86_64.tar.gz -C prebuilt/ros2_core/

# ONNX Runtime
tar xzf prebuilt/onnxruntime-linux-x64-1.21.0.tgz -C prebuilt/
mv prebuilt/onnxruntime-linux-x64-1.21.0 prebuilt/onnxruntime
```

```bash
# Build all packages (output goes to output/)
./build.sh

# Clean build (removes output/ first)
./build.sh -c

# Build specific packages
./build.sh --packages-select buddy_audio buddy_state_machine

# Run the robot (sources setup, detects/replaces stale processes)
./run.sh
```

## Testing

```bash
# Run tests for specific packages
colcon test --packages-select buddy_audio buddy_vision
colcon test-result --verbose

# Run a single test binary directly (after build)
./output/build/<pkg>/<test_binary>
```

Test framework: `ament_cmake_gtest`. Tests live in `src/<pkg>/test/`.

## Architecture

ROS 2 component workspace. All modules run as `rclcpp_components` in a single process with intra-process communication enabled. Entry point is `buddy_app/src/buddy_main.cpp`, which uses `class_loader` to dynamically load all component `.so` libraries, then drives lifecycle (configure → activate).

### Main Pipeline Flow

```
Audio → Brain → Vision (optional) → Cloud → Brain → Audio playback
```

1. `buddy_audio` — wake word, ASR, TTS playback
2. `buddy_brain` — state machine, dialog context, sentence segmentation
3. `buddy_vision` — image capture and emotion recognition
4. `buddy_cloud` — Doubao API multimodal requests
5. `buddy_brain` — streaming response segmentation → audio TTS

### Supporting Packages

- `buddy_interfaces` — custom `.msg`/`.srv` definitions (`CloudRequest`, `CloudChunk`, `Sentence`, `ExpressionResult`, `DisplayCommand`, `CaptureImage`)
- `buddy_app` — C++ entry point (`buddy_main`) that loads all components into one process
- `buddy_app/params/` — runtime parameter YAML files

### Communication Conventions

- Topics use module prefix + semantic name: `/audio/wake_word`, `/dialog/sentence`
- Services for request-response; topics for event streams
- New message fields must be backward-compatible
- Modify `.msg`/`.srv` → rebuild → update dependent package tests

## Formatting & Linting

```bash
# Format all C++ and CMake files in src/
./format_and_lint.sh

# Check only (CI mode, exits 1 if unformatted)
./format_and_lint.sh --check
```

Requires `clang-format` and `cmake-format` (`pip install clang-format cmake-format`).

## Code Style

- C++17, compiler flags: `-Wall -Wextra -Wpedantic`
- Classes: `PascalCase`, functions/variables: `snake_case`, test files: `test_<feature>_node.cpp`
- ROS 2 idioms: `rclcpp`, lifecycle/component nodes
- Runtime config in `buddy_app/params/*.yaml`; document new params in PR description

## Commit Format

Strict format: `feat(module): [PRO-10000] <Description>`

The prefix `feat(module): [PRO-10000] ` is a constant string — never change it. `<Description>` is imperative, capitalized, under 50 chars (e.g., `Add dialog timeout guard`).

## Key Docs

- `docs/architecture.md` — component topology and dependency strategy
- `docs/vision_architecture.md` — buddy_vision internal architecture and data flow
- `docs/communication_protocol.md` — ROS 2 topic/service contracts
- `docs/plan.md` — implementation roadmap
- `docs/models.md` — model preparation notes
