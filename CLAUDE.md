# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Build + test
./build.sh

# Clean rebuild + test
./build.sh -c

# Individual steps
./build.sh build    # Compile only
./build.sh test     # Test only
./build.sh clean    # Clean overlay artifacts

# Run (MUST source underlay first, then overlay)
source third_party/ros2/$ROS_DISTRO/install/setup.bash
source src/buddy_robot/install/setup.bash
ros2 launch buddy_bringup buddy.launch.py
```

## Build Layout (Underlay + Overlay)

The build script auto-detects Ubuntu version and selects the ROS2 distro:
- Ubuntu 22.04 → ROS2 Humble (`third_party/ros2/humble/`)
- Ubuntu 24.04 → ROS2 Jazzy (`third_party/ros2/jazzy/`)

```
third_party/ros2/{humble|jazzy}/ ← Underlay (pre-built, downloaded from GitHub Release)
  install/setup.bash               ~80 packages (downloaded tarball)

src/buddy_robot/                 ← Overlay (buddy_robot packages, compiled locally)
  install/setup.bash               8 packages (~2.6MB)
```

The underlay is downloaded from https://github.com/voicialex/ros2_core releases.
Version is pinned in `.ros_core_version` at the project root.

ALL artifacts (install/, build/, log/) under third_party/ros2/*/ and src/buddy_robot/ are gitignored.

## Architecture

ROS2 Jazzy workspace with 8 packages, all using LifecycleNode pattern.

**Packages:**
- **buddy_interfaces** — Custom msg/srv definitions (CloudChunk, DisplayCommand, ExpressionResult, Sentence, UserInput, CaptureImage)
- **buddy_audio** — Audio pipeline (wake word, ASR, TTS)
- **buddy_vision** — Vision pipeline (camera capture, expression recognition via RKNN)
- **buddy_cloud** — Cloud client (WebSocket gateway connection)
- **buddy_dialog** — Dialog manager (context and routing)
- **buddy_sentence** — Sentence segmenter (splits cloud stream into TTS-ready sentences)
- **buddy_state_machine** — State machine orchestrator (IDLE → LISTENING → THINKING → SPEAKING)
- **buddy_bringup** — Launch files and parameter configuration

**Node lifecycle:** `on_configure → on_activate → on_deactivate → on_cleanup → on_shutdown`

**Inter-node communication:** ROS2 topics (pub/sub) and services. StateMachineNode is the central coordinator.

**Data flow:**
```
WakeWord → StateMachine → ASR → StateMachine → UserInput → Cloud →
CloudChunk → Sentence → TTS → Audio → back to StateMachine
Expression → StateMachine (triggers vision capture via service call)
```

## Key Files

- `scripts/build_all.sh` — Overlay build script (auto-detects Ubuntu version)
- `scripts/setup_underlay.sh` — Downloads pre-built ROS2 underlay from GitHub Release
- `.ros_core_version` — Pins the ros2_core release version (e.g. `v2026.04.1`)
- `docs/architecture.md` — Architecture spec for ROS2 LifecycleNode design
- `docs/communication_protocol.md` — ROS2 topic/service definitions + cloud WebSocket protocol
- `docs/plan.md` — Phased implementation roadmap
- `src/buddy_robot/buddy_bringup/params/buddy_params.yaml` — Node parameters
- `src/buddy_robot/buddy_bringup/launch/buddy.launch.py` — Main launch file
- `src/buddy_robot/buddy_interfaces/` — Message and service IDL definitions

## Conventions

- C++17, `-Wall -Wextra -Wpedantic` always on
- ROS2 Jazzy (Ubuntu 24.04) / Humble (Ubuntu 22.04) — pre-built underlay downloaded from ros2_core GitHub Release
- Commit format: `feat(module): [PRO-10000] <Description>`
