# Buddy 多模态交互管线升级设计

版本: v2.0
日期: 2026-05-02
状态: 草案

## 1. 背景与目标

### 1.1 现状

Buddy 陪伴机器人当前有 7 个业务包：audio、vision、state_machine、dialog、sentence、cloud、interfaces。

经审计发现严重的过度拆分问题：
- `buddy_dialog`：3 行逻辑，只打日志，不转发不存储 — **死代码**
- `buddy_sentence`：66 行逻辑，纯文本切句 — **一个函数的活拆成了一个包**
- `buddy_state_machine`：15 行状态切换 — **逻辑过于单薄**

三个模块加起来不到 90 行业务逻辑，却各自有独立的 CMakeLists、头文件、LifecycleNode 样板。

### 1.2 目标

1. **架构精简**：删除 dialog、sentence，合并 state_machine 为新的 `buddy_brain`
2. **ASR 替换**：Vosk → Sherpa-ONNX（流式 ASR + 唤醒词）
3. **仲裁升级**：混合触发 — 语音带情绪上下文 + 强情绪主动触发
4. **多模态云端**：文本 + 摄像头截图 + 情绪 → 豆包大模型
5. **第二摄像头**：本次不做

硬件：x86 开发，后期迁移 ARM。

## 2. 新架构（4 个业务包）

```
┌──────────────────────────────────────────────────────────────┐
│                     buddy_main (单进程)                       │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐                          │
│  │ buddy_audio  │  │ buddy_vision │                          │
│  │              │  │              │                          │
│  │ AudioCapture │  │ CameraWorker │                          │
│  │ WakeWord     │  │ EmotionModel │                          │
│  │ StreamASR    │  │              │                          │
│  │ TTSPlayback  │  │              │                          │
│  └──────┬───────┘  └──────┬───────┘                          │
│         │                 │                                  │
│  ┌──────┴─────────────────┴───────────────────────────────┐  │
│  │                   buddy_brain                           │  │
│  │                                                         │  │
│  │  StateMachine   ← 仲裁：混合触发（语音+情绪）            │  │
│  │  DialogContext   ← 对话历史 + 多模态上下文组装            │  │
│  │  segment()       ← 流式文本切句（内联函数）              │  │
│  └──────────────────────────┬──────────────────────────────┘  │
│                             │                                │
│  ┌──────────────────────────┴──────────────────────────────┐  │
│  │                   buddy_cloud                            │  │
│  │           豆包 API（默认）/ Gemini（备选）                │  │
│  └──────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘

辅助包:
  buddy_interfaces  — 消息/服务定义
  buddy_app         — 入口程序 + 参数配置
```

### 2.1 为什么 cloud 单独保留

`buddy_cloud` 有清晰的职责边界：HTTP 网络 IO + provider 抽象 + 流式解析。它的依赖（libcurl/HTTP 库）和 brain 的依赖（纯逻辑）不同，测试策略也不同（mock HTTP vs 状态测试）。保持分离是合理的。

### 2.2 删除/合并清单

| 动作 | 包 | 原因 |
|------|-----|------|
| **删除** | `buddy_dialog` | 3 行日志代码，无任何业务逻辑 |
| **删除** | `buddy_sentence` | 66 行切句逻辑内联到 brain |
| **删除** | `buddy_state_machine` | 合并到 brain |
| **新建** | `buddy_brain` | 合并仲裁 + 对话上下文 + 切句 |

## 3. buddy_audio：ASR 替换

### 3.1 方案

Vosk → **Sherpa-ONNX**：统一 ONNX 推理栈，原生 C++ API，支持流式 ASR + 唤醒词，支持 ARM。

### 3.2 内部架构

```
buddy_audio (LifecycleNode)
├── AudioCapture          # ALSA 采集线程 → 16kHz PCM
├── WakeWordDetector      # Sherpa-ONNX keyword spotter
│   └── 检测唤醒词 → 发布 /audio/wake_word
├── StreamingASR          # Sherpa-ONNX 流式识别
│   └── VAD 静音 → 发布 /audio/asr_text
└── TTSPlayback           # 现有播放逻辑（不动）
```

### 3.3 Topic

| Topic | 类型 | 说明 |
|-------|------|------|
| `/audio/wake_word` | `std_msgs/Empty` | 唤醒词（新增） |
| `/audio/asr_text` | `std_msgs/String` | ASR 文本（已有） |
| `/audio/tts_done` | `std_msgs/Empty` | TTS 播放完成（已有） |

### 3.4 配置

```yaml
audio:
  ros__parameters:
    device: "default"
    sample_rate: 16000
    wake_word:
      model_path: "models/keyword"
      keywords: ["你好小伙伴"]
      sensitivity: 0.5
    asr:
      model_path: "models/asr"
      vad_silence_ms: 800
      max_duration_seconds: 15
```

### 3.5 依赖

- `prebuilt/sherpa-onnx/`（预编译库）
- `models/asr/`、`models/keyword/`（gitignored）

## 4. buddy_brain：仲裁 + 对话 + 切句

这是本次最核心的新模块。合并了原 state_machine + dialog + sentence 的职责。

### 4.1 内部结构

```cpp
class BrainNode : public rclcpp_lifecycle::LifecycleNode {
  // === 状态机 ===
  enum class State { IDLE, LISTENING, EMOTION_TRIGGER, REQUESTING, SPEAKING };
  State state_{State::IDLE};

  // === 对话上下文 ===
  std::deque<DialogTurn> history_;         // 最近 N 轮对话
  std::string current_emotion_;
  float emotion_confidence_{0.f};

  // === 情绪触发追踪 ===
  std::chrono::steady_clock::time_point negative_since_;
  std::chrono::steady_clock::time_point last_proactive_trigger_;

  // === 切句 ===
  std::string sentence_buffer_;
  std::vector<std::string> segment(const std::string &text);  // 内联函数
};
```

### 4.2 状态机

```
          ┌─────────┐
          │  IDLE   │ ← 监听唤醒词 + 持续接收情绪
          └────┬────┘
     唤醒词 ↓        ↓ 强情绪持续 N 秒
     ┌──────────┐  ┌──────────────┐
     │ LISTENING│  │EMOTION_TRIGGER│
     └────┬─────┘  └──────┬───────┘
    ASR完成↓          自动组装↓
     ┌──────────────────────┐
     │     REQUESTING       │ ← 组装多模态请求 → cloud
     └──────────┬───────────┘
         云端返回↓
     ┌──────────────────────┐
     │      SPEAKING        │ ← 切句 → TTS
     └──────────┬───────────┘
       播放完成 ↓
          回到 IDLE
```

### 4.3 数据流（brain 内部，无 IPC）

```
语音触发:
  on_wake_word() → state = LISTENING
  on_asr_text(text) → 组装 context{text, emotion, image} → 发布 /brain/cloud_request → state = REQUESTING

情绪触发:
  on_emotion(result) → 更新 emotion 追踪 → 超过阈值+冷却 → 组装 context → 发布 /brain/cloud_request → state = REQUESTING

云端返回:
  on_cloud_chunk(chunk) → segment(chunk.text) → 每完整句发布 /brain/sentence → chunk.is_final 时 state = SPEAKING

TTS 完成:
  on_tts_done() → state = IDLE
```

### 4.4 对话上下文组装

进入 REQUESTING 状态时，brain 组装完整请求：

```cpp
void request_cloud(const std::string &trigger_type, const std::string &user_text) {
  auto msg = buddy_interfaces::msg::CloudRequest();
  msg.trigger_type = trigger_type;
  msg.user_text = user_text;
  msg.emotion = current_emotion_;
  msg.emotion_confidence = emotion_confidence_;
  msg.dialog_history = serialize_history(history_);  // 最近 N 轮
  msg.system_prompt = system_prompt_;

  // 截图
  auto image = call_capture_service();  // /vision/emotion/capture
  if (image) msg.image = *image;

  cloud_request_pub_->publish(msg);
}
```

### 4.5 切句逻辑（内联）

从原 `buddy_sentence` 提取，约 30 行：

```cpp
// 按中英文句号、问号、感叹号切分
std::vector<std::string> BrainNode::segment(const std::string &text) {
  sentence_buffer_ += text;
  std::vector<std::string> sentences;
  // ... 按标点切分 buffer，返回完整句子
  return sentences;
}
```

### 4.6 Topic（brain 对外接口）

| 方向 | Topic | 类型 | 说明 |
|------|-------|------|------|
| 订阅 | `/audio/wake_word` | `std_msgs/Empty` | 唤醒词 |
| 订阅 | `/audio/asr_text` | `std_msgs/String` | ASR 文本 |
| 订阅 | `/audio/tts_done` | `std_msgs/Empty` | TTS 完成 |
| 订阅 | `/vision/emotion/result` | `EmotionResult` | 情绪结果 |
| 订阅 | `/cloud/response` | `CloudChunk` | 云端流式返回 |
| 发布 | `/brain/cloud_request` | `CloudRequest` | → cloud |
| 发布 | `/brain/sentence` | `Sentence` | 切好的句子 → audio TTS |
| 服务调用 | `/vision/emotion/capture` | `CaptureImage` | 截图 |

### 4.7 配置

```yaml
brain:
  ros__parameters:
    system_prompt_path: "prompts/buddy_system.txt"
    max_history_turns: 10
    emotion_trigger:
      enabled: true
      negative_emotions: ["sad", "angry", "fear"]
      confidence_threshold: 0.7
      duration_seconds: 3.0
      cooldown_seconds: 60.0
    voice_trigger:
      attach_image: true
```

## 5. buddy_cloud：多模态 API

### 5.1 Provider 抽象

```cpp
class CloudProvider {
public:
  virtual ~CloudProvider() = default;
  virtual void send_streaming(
      const CloudRequest &req,
      std::function<void(const std::string &chunk, bool is_final)> on_chunk) = 0;
};

class DoubaoProvider : public CloudProvider { ... };  // 默认
class GeminiProvider : public CloudProvider { ... };  // 备选
```

### 5.2 图片处理

截图 → 缩放到 `image_max_width` → JPEG 编码 → base64 → 嵌入 API multipart 请求。

### 5.3 配置

```yaml
cloud:
  ros__parameters:
    provider: "doubao"
    doubao:
      api_key: "${DOUBAO_API_KEY}"
      model: "doubao-1.5-pro"
      endpoint: "https://ark.cn-beijing.volces.com/api/v3"
    gemini:
      api_key: "${GEMINI_API_KEY}"
      model: "gemini-2.0-flash"
    image_max_width: 512
    timeout_seconds: 30
```

### 5.4 Topic

| 方向 | Topic | 类型 |
|------|-------|------|
| 订阅 | `/brain/cloud_request` | `CloudRequest` |
| 发布 | `/cloud/response` | `CloudChunk` |

## 6. buddy_interfaces 消息变更

### 新增

**CloudRequest.msg**:
```
string trigger_type           # "voice" | "emotion"
string user_text              # ASR 文本
string emotion                # 情绪标签
float32 emotion_confidence
string[] dialog_history       # 最近 N 轮对话
string system_prompt
sensor_msgs/Image image       # 截图（可选）
```

### 保留不变

- `EmotionResult.msg`
- `CloudChunk.msg`
- `Sentence.msg`
- `CaptureImage.srv`

### 可能删除

- `UserInput.msg` — 只在 dialog 中使用，dialog 已删除
- `DisplayCommand.msg` — 评估是否仍需要

## 7. 端到端流程

```
语音触发:
  唤醒词 → ASR → brain(组装文本+情绪+截图) → cloud(豆包) → brain(切句) → audio(TTS)

情绪触发:
  负面情绪持续3s → brain(组装情绪+截图) → cloud(豆包) → brain(切句) → audio(TTS)
```

对比之前：消息跳转从 6 步减少到 4 步，消除了 2 次无意义的 IPC。

## 8. 实施阶段

```
阶段0: 架构精简
  ├── 新建 buddy_brain 包
  ├── 迁移 state_machine 逻辑到 brain
  ├── 内联 sentence 切句函数到 brain
  ├── 删除 buddy_dialog、buddy_sentence、buddy_state_machine
  ├── 更新 buddy_main 组件加载列表
  └── 验证: 编译通过 + 现有测试通过
  预计: 2-3 天

阶段1: ASR 替换 (buddy_audio)
  ├── Sherpa-ONNX 集成 + 唤醒词
  ├── 流式 ASR + VAD
  └── 验证: 唤醒词 → ASR → /audio/asr_text 正确发布
  预计: 1-2 周

阶段2: 仲裁升级 (buddy_brain)
  ├── 混合触发状态机
  ├── 情绪追踪 + 主动触发
  ├── 对话历史管理
  └── 验证: 强情绪主动触发 + 语音带情绪上下文
  预计: 1 周

阶段3: 多模态云端 (buddy_brain + buddy_cloud)
  ├── CloudRequest 消息含图片+历史
  ├── cloud 豆包 API 适配（多模态）
  └── 验证: 云端收到图片+文本，返回合理回复
  预计: 1-2 周

阶段4: 端到端集成
  ├── 全链路冒烟测试
  ├── 延迟优化 + 参数调优
  └── 验证: 从唤醒词到 TTS 播放完整跑通
  预计: 3-5 天
```

## 9. 不在本次范围

- 第二摄像头（场景/物体识别）
- 多语言 ASR
- 本地 LLM
- ARM 交叉编译

## 10. 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| Sherpa-ONNX 中文精度不足 | ASR 质量差 | 评估多模型，必要时回退 |
| 豆包 API 图片限制 | 请求失败 | 压缩截图 |
| 情绪触发过频 | 烦人 | 冷却时间+阈值可调 |
| 多模态延迟 | 体验差 | 图片异步编码+流式返回 |
| 删包后遗漏引用 | 编译失败 | 阶段0 专门处理，全量编译验证 |
