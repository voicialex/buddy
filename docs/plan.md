# Buddy 会话管理与架构优化方案

## 一、已发现的问题

### P1: 用户打断时旧响应继续播放

**场景**: 用户问了问题 A，local/cloud 正在流式返回结果，TTS 正在播放。此时用户又说了问题 B。

**现状**: 新的 `request_inference` 调用会覆盖 `session_id_`，但旧的 TTS 队列里还有 A 的句子，audio pipeline 没有 turn 概念，无法区分哪些句子属于哪个 turn，导致 A 的残留句子和 B 的句子混在一起播放。

### P2: 上下文断裂 — 多轮对话无 session 概念

**场景**: 用户连续问了 3 个相关的问题。

**现状**: `history_` 无限累积，没有 session 边界。如果用户离开 10 分钟再回来，旧对话上下文还在，可能导致不相关的回复。没有超时清理机制。

### P3: Cloud 响应延迟导致双倍语音

**场景**: local 响应完成 → `on_local_chunk` 触发 SPEAKING → TTS 开始播放 → cloud 响应到达 → `on_cloud_chunk` 清空 `response_text_` 重新开始 → 又触发 SPEAKING → TTS 队列里同时有 local 和 cloud 的句子。

**现状**: `first_cloud_chunk_` 标志会重置 `response_text_`，但 audio pipeline 的 TTS 队列中已经 enqueue 的 local 句子无法被撤回。

### P4: SPEAKING 状态无超时保护

**场景**: TTS 播放完成，但 `/audio/tts_done` 消息丢失（网络抖动/进程卡顿）。

**现状**: Brain 永远卡在 SPEAKING 状态，无法回到 IDLE，机器人变成"死机"。

### P5: Cloud JSON 解析用字符串搜索

**现状**: `cloud_client_node.cpp:269-279` 用 `rfind("content\":\"")` 手动提取响应内容，遇到转义引号或嵌套 JSON 就会解析错误。

### P6: worker_thread 在回调中 join

**现状**: `local_llm_node` 和 `cloud_client_node` 的 `on_inference_request` 在订阅回调里调用 `worker_thread_.join()`，如果前一个请求还没完成，回调线程会被阻塞，导致 ROS executor 卡住。

---

## 二、实施方案

### Step 1: Session/Turn 管理（解决 P1 P2 P3 P4）

> 核心思路：给每个推理请求分配 turn_id，audio pipeline 按 turn_id 管理 TTS 队列，新 turn 自动清除旧队列。

#### 1.1 修改 `Sentence.msg` — 增加 `turn_id`

```
string session_id
string turn_id       # 新增：标识当前对话轮次
string text
uint32 index
bool is_final
```

#### 1.2 修改 `brain_node.hpp` — 增加 session/turn 状态

```cpp
// 新增成员变量
std::string turn_id_;
int turn_counter_{0};
rclcpp::TimerBase::SharedPtr session_timer_;
double session_timeout_seconds_{60.0};
```

#### 1.3 修改 `brain_node.cpp` — session/turn 生命周期

**`on_configure`**: 声明参数 + 创建 session 定时器

```cpp
declare_parameter("session_timeout_seconds", 60.0);
session_timeout_seconds_ = get_parameter("session_timeout_seconds").as_double();
```

**`request_inference`**: 每次请求分配新 turn_id

```cpp
turn_id_ = session_id_ + "-t" + std::to_string(++turn_counter_);
// 填入 req.turn_id（Step 2 Action 化后再加到 InferenceRequest）
```

**`reset_session_timer`**: 每次有用户活动时重置 1 分钟倒计时

```cpp
void reset_session_timer() {
  if (session_timer_) session_timer_->cancel();
  session_timer_ = create_wall_timer(
    std::chrono::duration<double>(session_timeout_seconds_),
    [this]() {
      RCLCPP_INFO(get_logger(), "Session timed out, clearing context");
      history_.clear();
      session_timer_->cancel();
    });
}
```

**`cancel_current_turn`**: 用户打断时发送空 sentence 通知 audio 清队列

```cpp
void cancel_current_turn() {
  auto msg = buddy_interfaces::msg::Sentence();
  msg.session_id = session_id_;
  msg.turn_id = turn_id_;
  msg.is_final = true;
  msg.text = "";
  sentence_pub_->publish(msg);
  // 撤回孤儿 user 历史
  if (!history_.empty() &&
      history_.back().rfind("user: ", 0) == 0)
    history_.pop_back();
}
```

**打断逻辑** — 在 `on_asr_text` 和 `on_emotion` 中：如果当前 state 不是 IDLE，先 `cancel_current_turn()` 再 `request_inference()`。

**SPEAKING 超时保护** — 在 `transition(SPEAKING)` 时启动 watchdog timer（例如 30 秒），超时自动回 IDLE，收到 `tts_done` 取消 timer。

```cpp
rclcpp::TimerBase::SharedPtr speaking_watchdog_;
// transition(SPEAKING) 时:
speaking_watchdog_ = create_wall_timer(std::chrono::seconds(30), [this]() {
  RCLCPP_WARN(get_logger(), "SPEAKING watchdog timeout, forcing IDLE");
  speaking_watchdog_->cancel();
  transition(State::IDLE);
});
// on_tts_done 时:
if (speaking_watchdog_) speaking_watchdog_->cancel();
```

#### 1.4 修改 `audio_pipeline_node` — 按 turn_id 管理 TTS 队列

```cpp
std::string current_turn_id_;

void on_sentence(const buddy_interfaces::msg::Sentence &msg) {
  // turn_id 变化 → 新 turn，清空旧队列
  if (!msg.turn_id.empty() && msg.turn_id != current_turn_id_) {
    std::lock_guard<std::mutex> lock(tts_queue_mtx_);
    std::queue<buddy_interfaces::msg::Sentence> empty;
    tts_queue_.swap(empty);  // 清空旧队列
    current_turn_id_ = msg.turn_id;
  }
  // is_final && text 为空 → 纯取消信号，不入队
  if (msg.is_final && msg.text.empty()) return;
  // 正常入队
  {
    std::lock_guard<std::mutex> lock(tts_queue_mtx_);
    tts_queue_.push(msg);
  }
  tts_queue_cv_.notify_one();
}
```

#### 1.5 修改 `brain.yaml` — 新增参数

```yaml
session_timeout_seconds: 60.0
speaking_watchdog_seconds: 30.0
```

#### Step 1 影响范围

| 文件 | 改动 |
|------|------|
| `buddy_interfaces/msg/Sentence.msg` | +1 字段 `turn_id` |
| `buddy_brain/include/buddy_brain/brain_node.hpp` | +5 成员变量 |
| `buddy_brain/src/brain_node.cpp` | +session/turn 方法, +打断逻辑, +watchdog |
| `buddy_audio/include/buddy_audio/audio_pipeline_node.hpp` | +1 成员 `current_turn_id_` |
| `buddy_audio/src/audio_pipeline_node.cpp` | `on_sentence` 增加 turn_id 判断 |
| `buddy_app/params/brain.yaml` | +2 参数 |

---

### Step 2: 推理链迁移到 ROS 2 Action（解决 P5 P6 + 真正的服务端取消）

> 核心思路：将 inference 从 topic 通信改为 ROS 2 Action，原生支持 Goal ID、Cancel、Feedback 流。

#### 2.1 定义 `buddy_interfaces/action/Inference.action`

```
# Goal
string trigger_type
string user_text
string emotion
float32 emotion_confidence
string system_prompt
string[] dialog_history
string session_id
string turn_id
sensor_msgs/Image image
---
# Result
string full_response
bool success
string error_message
---
# Feedback
string chunk_text
string session_id
string turn_id
bool is_final
```

#### 2.2 local_llm_node → Action Server

- `execute_callback` 替代 `on_inference_request`
- 每个 chunk 通过 `feedback` 发送
- 收到 cancel request 时终止 ollama curl 请求（设置 abort 标志）

#### 2.3 cloud_client_node → Action Server

- `execute_callback` 替代 `on_inference_request`
- 不再需要在回调中 `join` worker_thread（Action Server 自带线程管理）
- 用 `rclcpp_action` 的 `is_canceling()` 检查取消
- 引入轻量 JSON 解析替代字符串搜索（解决 P5）

#### 2.4 brain_node → Action Client

- `async_send_goal()` 替代 `inference_request_pub_->publish()`
- 保存 `GoalHandle` 用于 `async_cancel_goal()`
- `feedback_callback` 替代 `on_local_chunk` / `on_cloud_chunk` 订阅

#### Step 2 影响范围

| 文件 | 改动 |
|------|------|
| `buddy_interfaces/action/Inference.action` | 新文件 |
| `buddy_brain/src/brain_node.cpp` | publisher → action client |
| `buddy_local_llm/src/local_llm_node.cpp` | subscriber → action server |
| `buddy_cloud/src/cloud_client_node.cpp` | subscriber → action server + JSON 解析 |
| `CMakeLists.txt` (3 个包) | 增加 action 依赖 |

---

## 三、实施优先级

| 优先级 | 步骤 | 解决问题 | 复杂度 |
|--------|------|----------|--------|
| **先做** | Step 1: Session/Turn 管理 | P1 P2 P3 P4 | 中等（6 个文件，~80 行新代码） |
| **后做** | Step 2: Action 迁移 | P5 P6 + 真正服务端取消 | 较大（重构 3 个节点的通信模式） |

Step 1 独立于 Step 2，先完成 Step 1 即可解决 80% 的用户体验问题。Step 2 是架构升级，可以在 Step 1 稳定后择机实施。

---

## 四、风险

1. **Sentence.msg 加字段**: 需要 rebuild 所有依赖包（buddy_brain, buddy_audio），但 `turn_id` 有默认值（空字符串），对其他消费者向后兼容。
2. **session_timer 精度**: `create_wall_timer` 依赖 executor tick，如果 executor 阻塞，timer 会延迟触发。Step 2 的 Action 迁移会彻底解决 executor 阻塞问题。
3. **tts_queue_.swap 清空**: 如果 `tts_loop` 正在合成一句话，swap 不会中断当前合成，只影响后续队列。这是可接受的行为 — 正在播放的那句话会播完。
