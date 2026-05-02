# Buddy Robot 通信协议（ROS 2）

版本: v3.0
日期: 2026-05-03
状态: 当前有效

## 1. 范围

本规范定义仓库内 ROS 2 模块之间的通信约定。

## 2. 消息与服务定义

自定义接口位于：`src/buddy_interfaces`

- 消息：`InferenceRequest.msg`、`InferenceChunk.msg`、`Sentence.msg`、`EmotionResult.msg`
- 服务：`CaptureImage.srv`

修改接口时必须同步：

1. 更新 `.msg` / `.srv` 文件
2. 重新 `colcon build`
3. 更新依赖这些接口的包测试

## 3. Topic/Service 约束

1. Topic 命名保持"模块前缀 + 语义名"。

### Topic 列表（双脑架构）

| Topic | 发布者 | 订阅者 | 消息类型 |
|-------|--------|--------|----------|
| `/audio/wake_word` | buddy_audio | buddy_brain | std_msgs/String |
| `/audio/asr_text` | buddy_audio | buddy_brain | std_msgs/String |
| `/brain/request` | buddy_brain | buddy_cloud, buddy_local_llm | InferenceRequest |
| `/inference/local_chunk` | buddy_local_llm | buddy_brain | InferenceChunk |
| `/inference/cloud_chunk` | buddy_cloud | buddy_brain | InferenceChunk |
| `/brain/sentence` | buddy_brain | buddy_audio | Sentence |
| `/audio/tts_done` | buddy_audio | buddy_brain | std_msgs/Empty |
| `/vision/emotion/result` | buddy_vision | buddy_brain | EmotionResult |

### Service 列表

| Service | 服务端 | 客户端 | 类型 |
|---------|--------|--------|------|
| `/vision/emotion/capture` | buddy_vision | buddy_brain | CaptureImage |

2. 服务用于显式请求-响应场景，事件流优先 Topic。
3. 新增字段必须向后兼容。

## 4. 时序原则

1. 状态与上下文编排由 `buddy_brain` 负责。
2. 本地模型（ollama）先响应，brain 立即送 TTS 播放。
3. 云端模型（Doubao）到达后，brain 打断本地播放，替换为云端回复。
4. `buddy_audio` 发送播放完成信号，驱动主流程。