# Multi-Camera Video Capture for buddy_vision

**Date:** 2026-04-30
**Status:** Approved

## Overview

Add multi-camera video capture and model inference pipeline to `buddy_vision`. Two USB cameras (V4L2) run in parallel — one captures facial expressions, the other captures game画面 — each feeding its own model for independent inference. Current stage: PC development with mock models; will deploy to RK3588 embedded platform later.

## Requirements

- 2 USB cameras (V4L2) via OpenCV `VideoCapture`
- Parallel capture + independent inference per camera
- Non-blocking: capture thread never blocked by inference
- Model files packaged with the ROS 2 package (not system paths)
- Mock models for PC development; RKNN models for embedded deployment
- Backward-compatible message changes

## Architecture

### Single Node + Internal Multi-Threading

`VisionPipelineNode` (LifecycleNode) manages all cameras internally.

```
VisionPipelineNode
├── CameraWorker "expression"
│   ├── FrameBuffer (double-buffer)
│   ├── capture_thread   →  cap >> frame → buffer.write()
│   └── inference_thread →  buffer.snapshot() → ModelInterface::inference() → publish
├── CameraWorker "game"
│   ├── FrameBuffer (double-buffer)
│   ├── capture_thread   →  cap >> frame → buffer.write()
│   └── inference_thread →  buffer.snapshot() → ModelInterface::inference() → publish
└── CaptureImage services
    ├── /vision/expression/capture
    └── /vision/game/capture
```

### FrameBuffer: Mutex-Protected Double Buffer

Capture and inference threads communicate through a mutex-protected double buffer:

```cpp
class FrameBuffer {
    cv::Mat buffers_[2];
    int back_{0};
    bool has_frame_{false};
    std::mutex mtx_;

public:
    // Capture thread — writes to back, swaps
    void write(cv::Mat frame) {
        std::lock_guard<std::mutex> lock(mtx_);
        buffers_[back_] = std::move(frame);
        back_ = 1 - back_;
        has_frame_ = true;
    }

    // Inference thread — reads front (latest completed frame), returns clone
    bool snapshot(cv::Mat &out) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!has_frame_) return false;
        out = buffers_[1 - back_].clone();
        return true;
    }
};
```

**Why mutex instead of lock-free atomics:**
- Lock-free double-buffer has a subtle race: capture could flip `back_` twice before inference reads, causing it to read a partially-written buffer
- Mutex contention is negligible: write holds it for a pointer swap (~ns), snapshot holds it for clone (~1ms), and they run at very different frequencies (30fps vs 2fps)
- Correct and simple beats clever and broken

**Why double-buffer instead of ring buffer:**
- Inference only needs the latest frame; queued historical frames are wasted
- 2 slots, minimal overhead
- `clone()` ensures inference holds an independent copy while capture overwrites

### Model Interface

Abstract base class enables swapping between mock (PC) and RKNN (embedded):

```cpp
struct ModelResult {
    std::string label;
    float confidence;
};

class ModelInterface {
public:
    virtual ~ModelInterface() = default;
    virtual bool load(const std::string& model_path) = 0;
    virtual ModelResult inference(const cv::Mat& frame) = 0;
    virtual void unload() = 0;
};
```

Current stage: `MockModel` returns `{"neutral", 0.95f}`. Later: `RknnModel` loads `.rknn` files.

### Inference Pipeline per Camera

```
1. capture_thread: cap >> frame → buffer_.write(std::move(frame))
   (runs continuously, never blocked)

2. inference_thread: sleep(interval)
   → cv::Mat frame = buffer_.snapshot()
   → preprocess (resize to model_input_size, BGR→RGB, normalize)
   → ModelInterface::inference(frame)
   → build ExpressionResult message
   → publisher_->publish(msg)
```

## YAML Configuration

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

No `default_camera` — each camera name is known at instantiation. Separate services per camera.

## Model File Layout

```
buddy_vision/
├── models/
│   ├── expression/
│   │   └── model.rknn
│   └── game/
│       └── model.rknn
├── include/buddy_vision/
├── src/
└── test/
```

- CMake `install(DIRECTORY models/ ...)` copies models to package share directory
- Runtime path: `ament_index_get_package_share_directory("buddy_vision") + "/" + model_path`
- **`.rknn` files are large binaries — excluded from git via `.gitignore`**. During development, place them manually in `models/`. For deployment, the CI pipeline fetches them from a model registry or artifact store before build.

## Topics and Services

| Camera | Publish Topic | Service |
|--------|--------------|---------|
| expression | `/vision/expression/result` (`ExpressionResult`) | `/vision/expression/capture` (`CaptureImage`) |
| game | `/vision/game/result` (`ExpressionResult`) | `/vision/game/capture` (`CaptureImage`) |

### CaptureImage.srv — No Change

`CaptureImage.srv` remains unchanged. Each camera gets its own service instance (`/vision/expression/capture`, `/vision/game/capture`), so the request body does not need a camera selector — the service endpoint itself determines which camera to use.

```
# Request — empty
---
sensor_msgs/Image image
```

## Thread Lifecycle

Managed by LifecycleNode state transitions:

| Transition | Action |
|------------|--------|
| `on_configure` | Declare parameters, create publishers/services (no threads yet) |
| `on_activate` | For each camera: open VideoCapture, spawn capture_thread + inference_thread |
| `on_deactivate` | For each camera: set `stop_flag_`, join both threads, release VideoCapture |
| `on_cleanup` | Destroy publishers/services |

Each CameraWorker holds a `std::atomic<bool> running_{false}`. Threads check this flag in their loop:

```
capture_thread loop:    while (running_) { cap >> frame; buffer_.write(std::move(frame)); }
inference_thread loop:  while (running_) { sleep(interval); buffer_.snapshot() → inference → publish; }
```

On `on_deactivate`: set `running_ = false` → `thread.join()` → close VideoCapture. This ensures graceful shutdown without dangling threads.

## Breaking Changes

**Topic rename affects downstream consumers:**

| Before | After |
|--------|-------|
| `/vision/expression` | `/vision/expression/result` |

`buddy_state_machine` subscribes to `/vision/expression` — its subscription must be updated to `/vision/expression/result` (or `/vision/<camera>/result` for the camera it cares about). This is a coordinated change: both packages must be rebuilt together.

## Error Handling

| Condition | Behavior |
|-----------|----------|
| Camera open fails | Log warning, CameraWorker marked disconnected, other cameras unaffected |
| Camera disconnects mid-stream | Auto-reconnect with backoff (1s → 5s → 10s) |
| Model load fails | Camera captures but skips inference, logs error |
| Frame read timeout | Retry in capture thread loop, no impact on inference thread |

## File Changes Summary

| File | Action |
|------|--------|
| `buddy_vision/include/buddy_vision/frame_buffer.hpp` | New — double buffer |
| `buddy_vision/include/buddy_vision/model_interface.hpp` | New — abstract model + MockModel |
| `buddy_vision/include/buddy_vision/camera_worker.hpp` | New — per-camera capture + inference |
| `buddy_vision/include/buddy_vision/vision_pipeline_node.hpp` | Modify — multi-camera support |
| `buddy_vision/src/vision_pipeline_node.cpp` | Modify — instantiate CameraWorkers from config |
| `buddy_vision/CMakeLists.txt` | Modify — add new sources, install models/ |
| `buddy_vision/package.xml` | Modify — add opencv dependency |
| `buddy_app/params/vision.yaml` | Modify — new multi-camera config structure |

## Out of Scope

- RKNN model integration (separate spec)
- Image recording/saving
- Camera auto-discovery (hardcoded in config)
- GUI camera preview
