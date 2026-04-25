# Buddy Robot 架构文档（ROS2 Jazzy LifecycleNode）

版本: v5.0
日期: 2026-04-27
定位: 后续实现 Agent 的主输入文档

---

## 1. 设计结论

本项目采用 ROS2 Jazzy Jalisco 作为运行时框架:

1. 板端部署为 ROS2 工作空间（colcon 构建），每个功能领域封装为独立的 ROS2 包。
2. 每个功能节点实现为 `rclcpp_lifecycle::LifecycleNode`，具备标准化的生命周期管理。
3. 节点之间通过 ROS2 Topic（pub/sub）和 Service 通信，底层使用 DDS。
4. 云端模型使用豆包，通信采用:
   - 边缘 App -> 云网关: WebSocket
   - 云网关 -> 豆包: HTTPS + SSE (`stream=true`)
5. 多模态输入采用 文字 + 图片 URL（图片先上传对象存储，再传 URL）。
6. 中央状态机（StateMachineNode）编排所有节点的交互流程。

历史决策参见: `docs/compare_custom_vs_ros2.md`

---

## 2. 架构总览

```text
┌────────────────────────────────────────────────────────────┐
│                   buddy_bringup (launch)                   │
│   启动所有 LifecycleNode，加载参数文件                        │
├────────────────────────────────────────────────────────────┤
│                                                           │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────────┐ │
│  │ buddy_audio  │  │ buddy_vision │  │  buddy_cloud     │ │
│  │ (audio)      │  │ (vision)     │  │  (cloud)         │ │
│  │ KWS/ASR/TTS  │  │ Camera/RKNN  │  │  WebSocket       │ │
│  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘ │
│         │                 │                    │           │
│         ▼                 ▼                    ▼           │
│  ┌──────────────────────────────────────────────────────┐ │
│  │           buddy_state_machine (state_machine)         │ │
│  │  IDLE → LISTENING → THINKING → SPEAKING              │ │
│  └──────────┬───────────────────┬───────────────────────┘ │
│             │                   │                         │
│             ▼                   ▼                         │
│  ┌──────────────────┐  ┌───────────────────┐             │
│  │ buddy_dialog      │  │ buddy_sentence    │             │
│  │ (dialog)          │  │ (sentence)        │             │
│  │ 对话管理/路由      │  │ 流式分句          │             │
│  └──────────────────┘  └───────────────────┘             │
│                                                           │
│  ┌──────────────────┐                                     │
│  │ buddy_interfaces  │  msg/srv 定义                      │
│  └──────────────────┘                                     │
├────────────────────────────────────────────────────────────┤
│                   ROS2 Jazzy / DDS                        │
├────────────────────────────────────────────────────────────┤
│              Linux + RKNN Driver + ALSA + V4L2             │
└────────────────────────────────────────────────────────────┘
```

---

## 3. 包清单

| 包名 | 节点名 | 职责 | 生命周期 |
|------|--------|------|----------|
| buddy_interfaces | — | 自定义 msg/srv IDL | — |
| buddy_audio | `audio` | 唤醒词检测、ASR、TTS | LifecycleNode |
| buddy_vision | `vision` | 摄像头捕获、表情识别（RKNN NPU） | LifecycleNode |
| buddy_cloud | `cloud` | WebSocket 网关连接、流式响应 | LifecycleNode |
| buddy_dialog | `dialog` | 对话上下文管理、意图路由 | LifecycleNode |
| buddy_sentence | `sentence` | 云端流式文本分句（中英文标点） | LifecycleNode |
| buddy_state_machine | `state_machine` | 全局状态编排（IDLE/LISTENING/THINKING/SPEAKING） | LifecycleNode |
| buddy_bringup | — | launch 文件 + YAML 参数 | — |

---

## 4. 节点生命周期

所有功能节点继承 `rclcpp_lifecycle::LifecycleNode`，遵循 ROS2 标准生命周期:

```
[Unconfigured] → on_configure → [Inactive]
[Inactive] → on_activate → [Active]
[Active] → on_deactivate → [Inactive]
[Inactive] → on_cleanup → [Unconfigured]
[*] → on_shutdown → [Finalized]
```

- `on_configure`: 创建 publishers、subscribers、service servers，声明参数。
- `on_activate`: 启动业务逻辑（开始监听、连接云端等）。
- `on_deactivate`: 暂停业务，保留资源。

---

## 5. 通信拓扑

### 5.1 Topic 通信

| 发布方 | Topic | 消息类型 | 订阅方 |
|--------|-------|----------|--------|
| audio | `/audio/wake_word` | `std_msgs/String` | state_machine |
| audio | `/audio/asr_text` | `std_msgs/String` | state_machine |
| audio | `/audio/tts_done` | `std_msgs/String` | state_machine |
| sentence | `/dialog/sentence` | `buddy_interfaces/msg/Sentence` | audio |
| cloud | `/dialog/cloud_response` | `buddy_interfaces/msg/CloudChunk` | sentence, state_machine |
| state_machine | `/dialog/user_input` | `buddy_interfaces/msg/UserInput` | cloud, dialog |
| state_machine | `/display/command` | `buddy_interfaces/msg/DisplayCommand` | — (待实现) |
| vision | `/vision/expression` | `buddy_interfaces/msg/ExpressionResult` | state_machine |

### 5.2 Service 通信

| 服务端 | Service | 类型 | 客户端 |
|--------|---------|------|--------|
| vision | `/vision/capture` | `buddy_interfaces/srv/CaptureImage` | state_machine |

### 5.3 数据流（主对话链路）

```
1. 用户说唤醒词
   audio → /audio/wake_word → state_machine
   state_machine: IDLE → LISTENING

2. 用户说话
   audio → /audio/asr_text → state_machine
   state_machine: LISTENING → THINKING

3. 发送用户输入到云端
   state_machine → /dialog/user_input → cloud

4. 云端流式响应
   cloud → /dialog/cloud_response → sentence + state_machine

5. 分句后送 TTS
   sentence → /dialog/sentence → audio

6. TTS 播完
   audio → /audio/tts_done → state_machine
   state_machine: SPEAKING → IDLE
```

---

## 6. 消息定义（buddy_interfaces）

### Messages

| 消息 | 字段 | 用途 |
|------|------|------|
| `UserInput` | `text`, `session_id`, `timestamp` | 用户输入文本 |
| `CloudChunk` | `session_id`, `chunk_text`, `is_final` | 云端流式响应分块 |
| `Sentence` | `session_id`, `text`, `index` | 分句后的 TTS 就绪文本 |
| `ExpressionResult` | `expression`, `confidence`, `timestamp` | 表情识别结果 |
| `DisplayCommand` | `command`, `payload` | 显示控制命令 |

### Services

| 服务 | 请求 | 响应 | 用途 |
|------|------|------|------|
| `CaptureImage` | 空 | `sensor_msgs/Image` | 请求视觉节点抓帧 |

---

## 7. 状态机设计

`StateMachineNode` 维护全局状态:

```cpp
enum class State { IDLE, LISTENING, THINKING, SPEAKING };
```

状态转换:

| 当前状态 | 触发事件 | 目标状态 | 动作 |
|----------|----------|----------|------|
| IDLE | `/audio/wake_word` | LISTENING | 生成新 session_id |
| LISTENING | `/audio/asr_text` | THINKING | 发布 UserInput 到 cloud |
| THINKING | `/dialog/cloud_response` (first) | SPEAKING | — |
| THINKING | `/dialog/cloud_response` (is_final) | SPEAKING | 等待 TTS 完成 |
| SPEAKING | `/audio/tts_done` | IDLE | 会话结束 |
| * | — | — | 可选: 调用 `/vision/capture` 附加图片 |

---

## 8. 配置

参数通过 `buddy_bringup/params/buddy_params.yaml` 配置，launch 文件统一加载:

- **audio**: 模型路径、采样率、TTS 引擎、音量
- **vision**: 模型路径、摄像头设备、分辨率、推理间隔
- **cloud**: 网关 URL、重连退避参数
- **dialog**: 最大上下文轮数、人设

---

## 9. 云端通信方案

### 9.1 协议组合

1. 边缘到云网关: WebSocket + JSON
2. 云网关到豆包: HTTPS + SSE + JSON
3. 图片传输: 对象存储 URL（推荐）/ base64（小图兜底）

### 9.2 重连策略

`buddy_cloud` 节点内实现指数退避重连:
- 基础延迟: `reconnect_base_ms`（默认 200ms）
- 最大延迟: `reconnect_max_ms`（默认 800ms）

---

## 10. 关键时序（文本+图像对话）

1. `audio` 检测唤醒词 → 发布 `/audio/wake_word`
2. `state_machine` 转入 LISTENING → 生成 `session_id`
3. `audio` ASR 识别 → 发布 `/audio/asr_text`
4. `state_machine` 转入 THINKING → 可选调用 `/vision/capture` 抓帧
5. `state_machine` 发布 `/dialog/user_input`（含 text + 可选 image）
6. `cloud` 发 WebSocket 到网关 → 收到 SSE 流
7. `cloud` 发布 `/dialog/cloud_response` 分块
8. `sentence` 分句 → 发布 `/dialog/sentence`
9. `audio` TTS 播报 → 发布 `/audio/tts_done`
10. `state_machine` 转回 IDLE

---

## 11. 项目目录

```text
buddy_robot/
├── src/buddy_robot/          # ROS2 工作空间
│   ├── buddy_audio/          # src/, include/, test/, CMakeLists.txt, package.xml
│   ├── buddy_vision/
│   ├── buddy_cloud/
│   ├── buddy_dialog/
│   ├── buddy_sentence/
│   ├── buddy_state_machine/
│   ├── buddy_interfaces/     # msg/, srv/
│   └── buddy_bringup/        # launch/, params/
├── third_party/ros2/jazzy/   # Docker 源码编译配置
├── docs/                     # 设计文档
├── models/                   # AI 模型文件 (.gitignore)
└── scripts/                  # 辅助脚本
```

---

## 12. 关联文档

1. `docs/communication_protocol.md` — Topic/Service 详细字段定义 + 云端 WebSocket 协议
2. `docs/plan.md` — 分阶段实施计划
3. `docs/models.md` — 模型下载和配置指南
4. `docs/compare_custom_vs_ros2.md` — 自研 EventBus vs ROS2 迁移决策记录
