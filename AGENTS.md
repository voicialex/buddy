# Repository Guidelines

## Project Structure & Module Organization
This repository is a ROS 2 workspace centered on `src/`. Each runtime module is an `ament_cmake` package:
- `buddy_audio`, `buddy_vision`, `buddy_cloud`, `buddy_state_machine`, `buddy_dialog`, `buddy_sentence`
- `buddy_interfaces` for `.msg` / `.srv` contracts
- `buddy_bringup` for launch and parameter wiring (`launch/`, `params/`)

Tests live beside each package in `src/<package>/test/`. Design and migration notes are in `docs/`. External dependency artifacts are managed under `prebuilt/` (gitignored).

## Build, Test, and Development Commands
Run from repository root:
- `./build.sh` — validate `prebuilt/setup.bash`, source it, then build all packages to `output/`.
- `./build.sh --packages-select buddy_audio buddy_state_machine` — build selected packages only.
- `colcon test --packages-select buddy_audio buddy_vision` — run package tests.
- `colcon test-result --verbose` — print detailed test results.
- `source prebuilt/setup.bash` — manually load ROS 2 core dependency (or let `build.sh` handle it).
- `source output/install/setup.bash` — load local workspace overlay.

This repository no longer uses Conan metadata.

## Coding Style & Naming Conventions
Use C++17 and existing ROS 2 idioms (`rclcpp`, lifecycle/component nodes). Follow current file-local style; avoid reformat-only diffs. Prefer clear names:
- Classes: `PascalCase` (for example, `AudioPipelineNode`)
- Functions/variables: `snake_case`
- Test files: `test_<feature>_node.cpp`

Keep functions focused, comments short, and only where intent is not obvious.

## Testing Guidelines
Primary framework is `ament_cmake_gtest` with `ament_add_gtest(...)` in each package `CMakeLists.txt`. Add or update tests with every behavior change, especially lifecycle transitions and topic/service boundaries. Validate with targeted `colcon test --packages-select <pkg>` before running full test suites.

## Commit & Pull Request Guidelines
Commit format is strict:
- `feat(module): [PRO-10000] <Description>`

Keep `<Description>` imperative, capitalized, and under 50 characters (for example, `Add dialog timeout guard`).

PRs should include:
- scope (packages touched),
- rationale and user-visible impact,
- exact verification commands run,
- logs/screenshots for launch or runtime behavior changes.

## Security & Configuration Tips
Do not commit secrets, API keys, or large model artifacts. Keep runtime configuration in package `params/*.yaml` and document any new required parameter in the PR description.
