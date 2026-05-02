# buddy_vision 架构文档

版本: v1.0
日期: 2026-05-02
状态: 当前有效

## 1. 概述

`buddy_vision` 是视觉处理模块，负责摄像头采集、人脸检测和情绪识别。作为 ROS 2 Lifecycle 组件运行在 `buddy_main` 单进程中，支持多摄像头并行处理。

## 2. 架构总览

```
                     VisionPipelineNode (LifecycleNode)
                     ┌─────────────────────────────────────────────┐
  YAML params ──────►│ discover_camera_names()                     │
  (vision.yaml)      │ load_camera_config()                        │
                     │                                             │
                     │  ┌── CameraWorker ────────────────────────┐ │
                     │  │                                        │ │
                     │  │  capture_thread ──► FrameBuffer (双缓冲)│ │
                     │  │       │                    │            │ │
                     │  │       ▼                    ▼            │ │
                     │  │  [摄像头帧]        inference_thread     │ │
                     │  │                         │              │ │
                     │  │                    ModelInterface       │ │
                     │  │                   (EmotionOnnxModel)    │ │
                     │  │                         │              │ │
                     │  │                  result_callback ───────┼─┼──► /vision/<name>/result
                     │  │                                        │ │    (EmotionResult.msg)
                     │  └────────────────────────────────────────┘ │
                     │                                             │
                     │  /vision/<name>/capture ◄────────────────── │  (CaptureImage.srv)
                     └─────────────────────────────────────────────┘
```

每个摄像头对应一个 `CameraWorker` 实例，内含两个独立线程。

## 3. 类关系

```
ModelInterface (抽象接口)
├── EmotionOnnxModel    ONNX Runtime 情绪识别实现
└── MockModel           测试用 mock

FrameBuffer             双缓冲帧存储（线程安全）

CameraWorker            摄像头采集 + 推理调度
 ├── 持有 FrameBuffer
 └── 持有 ModelInterface

VisionPipelineNode (LifecycleNode)
 ├── 持有 map<name, CameraWorker>
 ├── 持有 map<name, Publisher<EmotionResult>>
 └── 持有 map<name, Service<CaptureImage>>
```

## 4. 数据流

1. **配置阶段** (`on_configure`): 从 ROS 参数发现摄像头名称，加载配置，创建 `CameraWorker`、Publisher、Service。
2. **激活阶段** (`on_activate`): 启动所有 `CameraWorker`。
3. **运行时**:
   - `capture_thread`: 循环读取摄像头帧 → 写入 `FrameBuffer`（双缓冲，无锁竞争）。
   - `inference_thread`: 按 `inference_interval_ms` 间隔从 `FrameBuffer` 取帧 → 调用 `ModelInterface::inference()` → 通过回调发布 `EmotionResult`。
4. **服务请求**: `/vision/<name>/capture` 返回最新帧的 `sensor_msgs/Image`。
5. **停用阶段** (`on_deactivate`): 停止所有 `CameraWorker`（join 线程、释放摄像头）。

## 5. 线程模型

每个摄像头产生 2 个线程：

| 线程 | 职责 | 频率 |
|------|------|------|
| `capture_thread` | V4L2 采集 + 帧写入 + 可选预览窗口 | 摄像头帧率 |
| `inference_thread` | 模型推理 + 结果回调 | `inference_interval_ms` |

线程间通过 `FrameBuffer`（双缓冲 + mutex）通信，无其他共享状态。设备断连时 `try_reconnect()` 采用线性退避（1s, 2s, 3s）重连。

## 6. Topic / Service

| 类型 | 名称 | 消息类型 | 说明 |
|------|------|----------|------|
| Topic | `/vision/<name>/result` | `EmotionResult` | 情绪识别结果 (emotion, confidence, timestamp) |
| Service | `/vision/<name>/capture` | `CaptureImage` | 请求最新帧 (→ sensor_msgs/Image) |

`<name>` 由参数配置决定，默认为 `emotion`。

## 7. 配置参数

参数定义在 `buddy_app/params/vision.yaml`，结构：

```yaml
vision:
  ros__parameters:
    cameras:
      emotion:                      # 摄像头名称
        device_path: "/dev/video0"  # V4L2 设备路径
        frame_width: 640
        frame_height: 480
        model_path: "models/emotion"  # 相对于 share/buddy_vision/
        model_input_width: 224
        model_input_height: 224
        inference_interval_ms: 200
        preview: false              # 是否显示 OpenCV 预览窗口
```

## 8. 外部依赖

| 依赖 | 用途 |
|------|------|
| OpenCV | 摄像头采集、图像预处理、人脸检测 (Haar Cascade) |
| ONNX Runtime | 情绪分类模型推理 |
| ROS 2 Lifecycle | 组件生命周期管理 |
| buddy_interfaces | EmotionResult.msg, CaptureImage.srv |

## 9. 文件清单

```
src/buddy_vision/
├── include/buddy_vision/
│   ├── camera_worker.hpp          # CameraWorker + CameraConfig
│   ├── frame_buffer.hpp           # FrameBuffer 双缓冲
│   ├── model_interface.hpp        # ModelInterface + MockModel
│   ├── onnx_emotion_model.hpp     # EmotionOnnxModel 声明
│   └── vision_pipeline_node.hpp   # VisionPipelineNode 声明
├── src/
│   ├── vision_pipeline_node.cpp   # 生命周期节点实现
│   └── onnx_emotion_model.cpp     # ONNX 情绪模型实现
├── test/
│   ├── test_frame_buffer.cpp
│   ├── test_model_interface.cpp
│   ├── test_vision_node.cpp
│   └── test_onnx_emotion_model.cpp
├── models/                        # 模型文件 (gitignored)
└── CMakeLists.txt
```
