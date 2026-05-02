# Buddy 多模态交互管线升级设计

版本: v1.0
日期: 2026-05-02
状态: 草案

## 1. 背景与目标

当前 Buddy 陪伴机器人已实现：情绪识别（单摄像头）、基础 ASR（Vosk）、简单状态机、纯文本云端请求、TTS 播放。

本次升级目标：
1. **ASR 替换**：Vosk → Sherpa-ONNX（流式 ASR + 唤醒词检测）
2. **仲裁升级**：混合触发 — 语音触发带情绪上下文 + 强情绪主动触发对话
3. **多模态云端**：发送文本 + 摄像头截图 + 情绪上下文给豆包大模型
4. **第二摄像头（场景识别）**：本次不做，预留接口

硬件：x86 开发，后期迁移 ARM。

## 2. 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                    buddy_main (单进程)                        │
│                                                             │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────────────┐ │
│  │ buddy_audio │  │ buddy_vision │  │ buddy_state_machine │ │
│  │             │  │              │  │     (仲裁器)         │ │
│  │ AudioCapture│  │ CameraWorker │  │                     │ │
│  │ WakeWord    │──│ EmotionModel │──│  IDLE               │ │
│  │ StreamASR   │  │              │  │  LISTENING          │ │
│  │ TTSPlayback │  │              │  │  EMOTION_TRIGGER    │ │
│  └──────┬──────┘  └──────┬───────┘  │  REQUESTING_CLOUD   │ │
│         │                │          │  SPEAKING            │ │
│         │                │          └──────────┬──────────┘ │
│         │                │                     │            │
│  ┌──────┴────────────────┴─────────────────────┴──────────┐ │
│  │                    buddy_dialog                         │ │
│  │          多模态上下文组装（文本+情绪+截图）                │ │
│  └─────────────────────────┬──────────────────────────────┘ │
│                            │                                │
│  ┌─────────────────────────┴──────────────────────────────┐ │
│  │                    buddy_cloud                          │ │
│  │          豆包多模态 API（默认）/ Gemini（备选）           │ │
│  └─────────────────────────┬──────────────────────────────┘ │
│                            │                                │
│  ┌─────────────────────────┴──────────────────────────────┐ │
│  │                   buddy_sentence                        │ │
│  │                   流式文本切句                            │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## 3. 阶段 1：ASR 替换（buddy_audio）

### 3.1 方案

Vosk → **Sherpa-ONNX**。理由：
- 项目已有 ONNX Runtime，统一推理栈
- 原生 C/C++ API，无需 Python 胶水
- 支持流式 ASR + 关键词检测（唤醒词）
- 支持 ARM 交叉编译

### 3.2 内部架构

```
buddy_audio (LifecycleNode)
├── AudioCapture          # ALSA/PulseAudio 采集线程
│   └── 环形缓冲区 → 16kHz PCM 音频帧
├── WakeWordDetector      # Sherpa-ONNX keyword spotter
│   └── 检测到唤醒词 → 发布 /audio/wake_word
├── StreamingASR          # Sherpa-ONNX 流式识别
│   └── 唤醒后识别 → VAD 静音 → 发布 /audio/asr_text
└── TTSPlayback           # 现有播放逻辑（不动）
```

### 3.3 工作流

1. `AudioCapture` 持续采集麦克风 16kHz PCM
2. 音频帧送入 `WakeWordDetector`
3. 检测到唤醒词 → 发布 `/audio/wake_word` → 切换 ASR 模式
4. `StreamingASR` 流式识别，VAD 静音检测 → 发布最终文本到 `/audio/asr_text`
5. 回到唤醒词监听

### 3.4 Topic

| Topic | 消息类型 | 说明 |
|-------|----------|------|
| `/audio/wake_word` | `std_msgs/Empty` | 唤醒词触发（新增） |
| `/audio/asr_text` | `std_msgs/String` | ASR 最终文本（已有） |
| `/audio/tts_done` | `std_msgs/Empty` | TTS 播放完成（已有） |

### 3.5 依赖

- Sherpa-ONNX 预编译库：`prebuilt/sherpa-onnx/`
- ASR 模型文件：`models/asr/`（gitignored）
- 唤醒词模型：`models/keyword/`（gitignored）

### 3.6 配置

```yaml
audio:
  ros__parameters:
    device: "default"                    # ALSA 设备
    sample_rate: 16000
    wake_word:
      model_path: "models/keyword"
      keywords: ["你好小伙伴"]
      sensitivity: 0.5
    asr:
      model_path: "models/asr"
      vad_silence_ms: 800               # 静音多久判定结束
      max_duration_seconds: 15           # 单次最长录音
```

## 4. 阶段 2：仲裁逻辑升级（buddy_state_machine）

### 4.1 混合触发模式

两条触发路径：

**路径 1 — 语音触发**：用户说唤醒词 → ASR → 组装请求（附加当前情绪）→ 云端

**路径 2 — 情绪主动触发**：情绪连续 N 秒为负面 → 机器人主动发起关心 → 云端

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
     │   REQUESTING_CLOUD   │
     └──────────┬───────────┘
         云端返回↓
     ┌──────────────────────┐
     │      SPEAKING        │ ← TTS 播放
     └──────────┬───────────┘
       播放完成 ↓
          回到 IDLE
```

### 4.3 情绪触发逻辑

```
持续记录最近 emotion_window 秒的情绪:
  if 负面情绪占比 > threshold AND 置信度 > confidence_threshold:
    if 距离上次主动触发 > cooldown:
      触发 EMOTION_TRIGGER
```

### 4.4 新增接口

**CloudRequest.msg**（新增）:
```
string trigger_type        # "voice" | "emotion"
string asr_text            # 语音触发时的 ASR 文本
string current_emotion     # 当前情绪标签
float32 emotion_confidence # 置信度
bool attach_image          # 是否附加截图
```

| 名称 | 类型 | 消息 | 说明 |
|------|------|------|------|
| `/arbitration/cloud_request` | Topic | `CloudRequest` | 仲裁结果 → dialog 组装 |

### 4.5 配置

```yaml
state_machine:
  ros__parameters:
    emotion_trigger:
      enabled: true
      negative_emotions: ["sad", "angry", "fear"]
      confidence_threshold: 0.7
      duration_seconds: 3.0
      cooldown_seconds: 60.0
    voice_trigger:
      attach_image: true            # 语音触发时也截图
```

## 5. 阶段 3：多模态云端请求（buddy_dialog + buddy_cloud）

### 5.1 buddy_dialog 改造

收到 `CloudRequest` 后组装多模态上下文：

1. 对话历史（最近 N 轮）
2. ASR 文本（语音触发时）
3. 情绪标签 + 置信度
4. 摄像头截图（通过 `/vision/emotion/capture` 服务获取）
5. System prompt（角色设定）

**MultimodalCloudRequest.msg**（新增）:
```
string system_prompt
string[] dialog_history
string user_text
string emotion
float32 emotion_confidence
sensor_msgs/Image image
string trigger_type
```

发布到 `/dialog/cloud_request_full`。

### 5.2 buddy_cloud 改造

抽象 provider 接口，默认豆包：

```cpp
class CloudProvider {
public:
  virtual ~CloudProvider() = default;
  virtual void send_streaming(
      const MultimodalCloudRequest &req,
      std::function<void(const std::string &chunk)> on_chunk) = 0;
};

class DoubaoProvider : public CloudProvider { ... };
class GeminiProvider : public CloudProvider { ... };  // 备选
```

图片处理：截图 → 缩放到 max_width → JPEG 编码 → base64 → 嵌入 API 请求。

### 5.3 配置

```yaml
cloud:
  ros__parameters:
    provider: "doubao"              # 默认豆包
    doubao:
      api_key: "${DOUBAO_API_KEY}"  # 环境变量
      model: "doubao-1.5-pro"
      endpoint: "https://ark.cn-beijing.volces.com/api/v3"
    gemini:                         # 备选
      api_key: "${GEMINI_API_KEY}"
      model: "gemini-2.0-flash"
    system_prompt_path: "prompts/buddy_system.txt"
    max_history_turns: 10
    image_max_width: 512
    timeout_seconds: 30
```

### 5.4 端到端流程

```
语音触发:
  唤醒词 → ASR → StateMachine(voice) → Dialog(文本+情绪+截图) → Cloud(豆包) → Sentence → TTS

情绪触发:
  负面情绪持续3s → StateMachine(emotion) → Dialog(情绪+截图) → Cloud(豆包) → Sentence → TTS
```

## 6. 新增 buddy_interfaces 消息

| 消息/服务 | 文件 | 说明 |
|-----------|------|------|
| `CloudRequest.msg` | msg/CloudRequest.msg | 仲裁 → dialog |
| `MultimodalCloudRequest.msg` | msg/MultimodalCloudRequest.msg | dialog → cloud |

现有消息保持不变（`EmotionResult`、`CloudChunk`、`Sentence`、`CaptureImage`）。

## 7. 实施阶段规划

```
阶段1: ASR 替换 (buddy_audio)
  ├── Sherpa-ONNX 集成 + 唤醒词
  ├── 流式 ASR + VAD
  └── 验证: 能识别语音并发布 /audio/asr_text
  预计: 1-2 周

阶段2: 仲裁升级 (buddy_state_machine + buddy_interfaces)
  ├── 新增 CloudRequest 消息
  ├── 混合触发状态机
  └── 验证: 强情绪主动触发 + 语音带情绪上下文
  预计: 1 周

阶段3: 多模态云端 (buddy_dialog + buddy_cloud)
  ├── 新增 MultimodalCloudRequest 消息
  ├── dialog 多模态上下文组装
  ├── cloud 豆包 API 适配（含图片）
  └── 验证: 云端收到图片+文本，返回合理回复
  预计: 1-2 周

阶段4: 端到端集成
  ├── 全链路冒烟测试
  ├── 延迟优化
  └── 参数调优
  预计: 3-5 天
```

## 8. 不在本次范围

- 第二摄像头（场景/物体识别）— 待定
- 多语言 ASR — 未来
- 本地 LLM — 未来
- ARM 交叉编译 — 后期迁移阶段

## 9. 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| Sherpa-ONNX 中文识别精度不足 | ASR 质量差 | 评估多个模型，必要时回退 Vosk 或换方案 |
| 豆包 API 图片大小限制 | 请求失败 | 压缩截图，降分辨率 |
| 情绪主动触发过于频繁 | 烦人 | 冷却时间 + 置信度阈值可调 |
| 多模态请求延迟过高 | 交互体验差 | 图片异步编码，流式返回 |
