# Buddy Robot 通信协议规范

版本: v2.0
日期: 2026-04-27
状态: 执行中

---

## 1. 目标与范围

本规范定义两类通信:

1. **节点间通信**: ROS2 节点间的 Topic（pub/sub）和 Service 通信。
2. **云端通信**: 边缘节点到网关的 WebSocket 协议，以及网关到模型服务 SSE 语义映射。

---

## 2. 统一设计约束

1. 统一编码: UTF-8。
2. 统一请求标识: `session_id`（一次对话会话内唯一，由 StateMachineNode 生成）。
3. 新增 msg/srv 字段保持向后兼容（ROS2 自动处理）。

---

## 3. ROS2 Topic 定义

### 3.1 Topic 总览

| Topic | 消息类型 | 发布方 | 订阅方 | 用途 |
|-------|----------|--------|--------|------|
| `/audio/wake_word` | `std_msgs/String` | audio | state_machine | 唤醒词检测 |
| `/audio/asr_text` | `std_msgs/String` | audio | state_machine | ASR 识别结果 |
| `/audio/tts_done` | `std_msgs/String` | audio | state_machine | TTS 播报完成 |
| `/dialog/user_input` | `buddy_interfaces/msg/UserInput` | state_machine | cloud, dialog | 用户输入 |
| `/dialog/cloud_response` | `buddy_interfaces/msg/CloudChunk` | cloud | sentence, state_machine | 云端流式响应 |
| `/dialog/sentence` | `buddy_interfaces/msg/Sentence` | sentence | audio | 分句后的 TTS 文本 |
| `/vision/expression` | `buddy_interfaces/msg/ExpressionResult` | vision | state_machine | 表情识别结果 |
| `/display/command` | `buddy_interfaces/msg/DisplayCommand` | state_machine | — (待实现) | 显示控制 |

### 3.2 Service 定义

| Service | 类型 | 服务端 | 客户端 | 用途 |
|---------|------|--------|--------|------|
| `/vision/capture` | `buddy_interfaces/srv/CaptureImage` | vision | state_machine | 请求抓帧 |

---

## 4. 消息字段定义（buddy_interfaces）

### 4.1 UserInput

```
string text              # 用户输入文本
string session_id        # 会话 ID
builtin_interfaces/Time timestamp  # 时间戳
```

发布方: state_machine
订阅方: cloud, dialog

### 4.2 CloudChunk

```
string session_id        # 会话 ID
string chunk_text        # 流式文本片段
bool is_final            # 是否最后一个分块
```

发布方: cloud
订阅方: sentence, state_machine

### 4.3 Sentence

```
string session_id        # 会话 ID
string text              # 分句文本
uint32 index             # 分句序号
```

发布方: sentence
订阅方: audio

### 4.4 ExpressionResult

```
string expression        # 表情类别
float32 confidence       # 置信度
builtin_interfaces/Time timestamp
```

发布方: vision
订阅方: state_machine

### 4.5 DisplayCommand

```
string command           # 命令类型
string payload           # JSON 载荷
```

发布方: state_machine
订阅方: 待实现

### 4.6 CaptureImage (Service)

```
# Request: 空
---
# Response:
sensor_msgs/Image image  # 抓取的图像
```

服务端: vision
客户端: state_machine

---

## 5. 节点间时序（主对话链路）

```text
1. audio → /audio/wake_word → state_machine
   state_machine: IDLE → LISTENING, 生成 session_id

2. audio → /audio/asr_text → state_machine
   state_machine: LISTENING → THINKING

3. state_machine → /dialog/user_input → cloud
   (可选: state_machine 调用 /vision/capture 获取图像)

4. cloud → /dialog/cloud_response → sentence + state_machine
   (多块流式, 最后 is_final=true)

5. sentence → /dialog/sentence → audio
   (按标点分句, 逐句送 TTS)

6. audio → /audio/tts_done → state_machine
   state_machine: SPEAKING → IDLE
```

---

## 6. 云端通信协议（边缘 <-> 网关）

### 6.1 WebSocket 连接

连接方式:

1. URL: `wss://<gateway_host>/v1/companion/session`
2. Header: `Authorization: Bearer <token>`
3. Header: `X-Device-Id: <device_id>`

保活策略:

1. 边缘每 10 秒发 ping。
2. 30 秒无 pong 或无业务包，判定断链并重连。

### 6.2 WebSocket 消息封装

所有 WS 消息统一 envelope:

```json
{
  "type": "chat.request",
  "request_id": "req-20260427-0001",
  "session_id": "sess-abc123",
  "ts_ms": 1777080000200,
  "payload": {}
}
```

`type` 枚举:

1. `chat.request`: 边缘发起请求。
2. `chat.delta`: 网关回传增量。
3. `chat.done`: 网关回传结束。
4. `chat.error`: 网关回传错误。
5. `session.ping`: 边缘心跳。
6. `session.pong`: 网关心跳回应。

### 6.3 边缘上行 `chat.request`

```json
{
  "type": "chat.request",
  "request_id": "req-20260427-0001",
  "session_id": "sess-abc123",
  "ts_ms": 1777080000200,
  "payload": {
    "model": "doubao-1.5-vision-pro-32k",
    "stream": true,
    "messages": [
      {
        "role": "user",
        "content": [
          {"type": "text", "text": "请帮我看看这张图"},
          {"type": "image_url", "image_url": {"url": "https://example.com/snap.jpg"}}
        ]
      }
    ],
    "temperature": 0.7,
    "max_tokens": 512
  }
}
```

### 6.4 网关下行 `chat.delta`

```json
{
  "type": "chat.delta",
  "request_id": "req-20260427-0001",
  "session_id": "sess-abc123",
  "ts_ms": 1777080000300,
  "payload": {
    "delta": "你好，",
    "index": 0
  }
}
```

### 6.5 网关下行 `chat.done`

```json
{
  "type": "chat.done",
  "request_id": "req-20260427-0001",
  "session_id": "sess-abc123",
  "ts_ms": 1777080000900,
  "payload": {
    "finish_reason": "stop",
    "usage": {
      "prompt_tokens": 120,
      "completion_tokens": 88,
      "total_tokens": 208
    }
  }
}
```

### 6.6 网关下行 `chat.error`

```json
{
  "type": "chat.error",
  "request_id": "req-20260427-0001",
  "session_id": "sess-abc123",
  "ts_ms": 1777080000500,
  "payload": {
    "code": "UPSTREAM_TIMEOUT",
    "message": "doubao stream timeout",
    "retryable": true
  }
}
```

---

## 7. 网关到模型服务（SSE）语义映射

网关内部调用模型服务采用 HTTPS + SSE（`stream=true`），映射到 WS 下行事件:

1. SSE `delta` → WS `chat.delta`
2. SSE `finish` → WS `chat.done`
3. SSE/HTTP 错误 → WS `chat.error`

边缘端只处理 WebSocket envelope，不直接消费 SSE。

---

## 8. 错误码与重试策略

标准错误码:

| 错误码 | 说明 | 可重试 |
|--------|------|--------|
| `BAD_REQUEST` | 参数非法 | 否 |
| `UNAUTHORIZED` | 鉴权失败 | 否 |
| `RATE_LIMITED` | 限流 | 是，指数退避 |
| `UPSTREAM_TIMEOUT` | 上游超时 | 是 |
| `UPSTREAM_UNAVAILABLE` | 上游不可用 | 是 |
| `INTERNAL_ERROR` | 网关内部错误 | 是 |

重试策略:

1. 仅 `retryable=true` 才重试。
2. 间隔: `reconnect_base_ms`（默认 200ms），指数退避到 `reconnect_max_ms`（默认 800ms）。
3. 达到上限后标记 `is_final=true`，终止当前请求。

---

## 9. 安全要求

1. WebSocket 必须使用 TLS（wss）。
2. Token 不写日志明文。
3. `image_url` 若含签名参数需脱敏。
4. `session_id` 可追踪但不可推导用户隐私。

---

## 10. 版本策略

1. 文档版本: `v2.0`（ROS2 版本）。
2. 新增 msg/srv 字段保持向后兼容。
3. 破坏性变更需要新消息类型或服务定义。
