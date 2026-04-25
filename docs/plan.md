# Buddy Robot 实施计划（ROS2 Jazzy）

版本: v2.0
日期: 2026-04-27
状态: 执行中

---

## 1. 文档关系

1. `docs/architecture.md` — ROS2 LifecycleNode 架构、包间通信拓扑
2. `docs/communication_protocol.md` — ROS2 Topic/Service 定义 + 云端 WebSocket 协议

---

## 2. 当前状态总览

### 2.1 已完成

1. ROS2 Jazzy 工作空间搭建（8 个包，colcon 构建）。
2. 所有 LifecycleNode 骨架实现（audio / vision / cloud / dialog / sentence / state_machine）。
3. 自定义消息接口定义（buddy_interfaces: 5 msg + 1 srv）。
4. Launch 文件 + YAML 参数配置（buddy_bringup）。
5. SentenceSegmenter 中英文分句算法实现。
6. StateMachine 状态转换逻辑（IDLE → LISTENING → THINKING → SPEAKING）。
7. 所有 26 个单元测试通过。
8. 自研 EventBus 框架迁移到 ROS2（历史记录见 `compare_custom_vs_ros2.md`）。

### 2.2 主要待办

1. audio 节点接入真实 sherpa-onnx（KWS/ASR/TTS）。
2. vision 节点接入真实摄像头 + RKNN NPU 推理。
3. cloud 节点接入真实 WebSocket 网关。
4. dialog 节点实现真实对话上下文管理。
5. 状态机完善打断（interrupt）语义。
6. 部署到 RK3588 板端验证。

---

## 3. 架构对齐矩阵

| 包 | 当前状态 | 下一步动作 |
|---|---|---|
| buddy_interfaces | 已完成 msg/srv 定义 | 随需求扩展 |
| buddy_audio | 骨架（生命周期可跑） | 接入 sherpa-onnx KWS/ASR/TTS |
| buddy_vision | 骨架（生命周期可跑） | 接入 V4L2 摄像头 + RKNN 表情推理 |
| buddy_cloud | 骨架（生命周期可跑） | 接入 WebSocket 网关 + SSE 流式解析 |
| buddy_dialog | 骨架（仅日志） | 实现对话上下文管理、意图路由 |
| buddy_sentence | 已实现分句算法 | 完善边界情况（emoji、混合语言） |
| buddy_state_machine | 状态转换已实现 | 完善 interrupt 语义、会话管理 |
| buddy_bringup | Launch + 参数已配置 | 增加运行时参数动态调整 |

---

## 4. 分阶段执行计划

### Phase P0: 真实能力接入（当前阶段）

目标: 让每个节点从骨架升级为可运行的真实功能。

任务:

1. **buddy_audio** — 集成 sherpa-onnx:
   - KWS: 加载唤醒词模型，检测唤醒词发布 `/audio/wake_word`
   - ASR: 加载 Zipformer 模型，识别结果发布 `/audio/asr_text`
   - TTS: 订阅 `/dialog/sentence`，合成语音播放，完成后发布 `/audio/tts_done`

2. **buddy_vision** — 集成 V4L2 + RKNN:
   - 打开 `/dev/video0`，实现 `/vision/capture` service
   - 加载 RKNN 模型，识别人脸表情，发布 `/vision/expression`

3. **buddy_cloud** — 集成 WebSocket:
   - 连接网关，订阅 `/dialog/user_input` 发送请求
   - 解析 SSE 流式响应，发布 `/dialog/cloud_response`

验收标准:

1. 每个节点 `on_activate` 后能执行真实业务逻辑。
2. 全链路可跑通一次"唤醒 → 对话 → 播报"流程。

---

### Phase P1: 对话与状态机完善

目标: 支持多轮对话、打断、上下文管理。

任务:

1. **buddy_dialog** — 实现对话上下文:
   - 维护 session 粒度的对话历史
   - 路由用户意图到对应处理链路

2. **buddy_state_machine** — 完善:
   - 按 session_id 管理会话状态
   - 实现 interrupt（打断当前播报，回到 LISTENING）
   - 支持视觉触发（表情变化触发主动问候）

3. **buddy_sentence** — 增强:
   - 完善 emoji、混合语言分句边界

验收标准:

1. 多轮对话不串话。
2. 打断后不继续播放旧内容。

---

### Phase P2: 板端部署与稳定

目标: 在 RK3588 上稳定运行。

任务:

1. 编写部署文档和 systemd service。
2. RKNN NPU 性能调优（帧率、推理间隔）。
3. 24 小时稳定性测试。
4. 网络断连/重连场景验证。

验收标准:

1. 板端开机能自启动。
2. 24 小时无崩溃。

---

## 5. 里程碑

| 里程碑 | 阶段 | 输出物 |
|--------|------|--------|
| M1 | P0 完成 | 全链路可演示版本 |
| M2 | P1 完成 | 多轮对话 + 打断功能 |
| M3 | P2 完成 | 板端稳定运行版本 |

---

## 6. 风险与缓解

1. **风险**: sherpa-onnx 在 RK3588 上 RTF 不达标。
   **缓解**: 使用 NPU 加速的模型版本，备选减少采样率。

2. **风险**: WebSocket 网关不稳定。
   **缓解**: 实现指数退避重连（200/400/800ms），保留 mock 模式。

3. **风险**: RKNN 模型精度不够。
   **缓解**: 量化校准数据集扩充，备选更高精度模型。

---

## 7. 执行约束

1. 所有新增 msg/srv 字段必须向后兼容。
2. 每个包独立测试，`colcon test` 通过后才合并。
3. 每完成一个 Phase 补齐对应文档。
