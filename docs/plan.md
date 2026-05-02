# Buddy 多模态管线升级 — 进度追溯

版本: v1.0
日期: 2026-05-02
分支: `feature/ros2`

---

## 已完成

### 阶段 0：架构精简

| 项目 | 状态 | 提交 | 说明 |
|------|------|------|------|
| 删除 `buddy_dialog` | ✅ | `4b4796b` | 死代码，3 行逻辑 |
| 删除 `buddy_sentence` | ✅ | `4b4796b` | 一个函数拆成了一个包 |
| 删除 `buddy_state_machine` | ✅ | `4b4796b` | 15 行逻辑 |
| 新建 `buddy_brain` | ✅ | `e0b3c93` | 合并状态机 + 对话上下文 + 切句 |
| `buddy_main` 更新 | ✅ | `4b4796b` | 组件列表从 7 → 4 |
| 文档同步 | ✅ | `05a1722` | architecture.md, communication_protocol.md, CLAUDE.md |

**变更文件：**
- `src/buddy_brain/` — 整个包（新建）
- `src/buddy_app/src/buddy_main.cpp:16-22` — 组件列表
- `src/buddy_app/params/modules.yaml` — 4 模块
- `src/buddy_app/params/brain.yaml` — brain 参数

### 阶段 1：消息接口升级

| 项目 | 状态 | 提交 | 说明 |
|------|------|------|------|
| `CloudRequest.msg` | ✅ | `e97d1d5` | brain→cloud 多模态请求消息 |
| Topic 重命名 | ✅ | `4b4796b` | `/dialog/*` → `/brain/*`, `/cloud/*` |

**变更文件：**
- `src/buddy_interfaces/msg/CloudRequest.msg` — 新消息
- `src/buddy_interfaces/CMakeLists.txt:17` — 注册新消息

### 阶段 2：云端多模态（豆包 API）

| 项目 | 状态 | 提交 | 说明 |
|------|------|------|------|
| Doubao HTTP API 集成 | ✅ | `30442f4` | libcurl POST，base64 图片编码 |
| 流式 CloudChunk 发布 | ✅ | `30442f4` | 逐 chunk 解析 SSE 响应 |
| 环境变量 API Key | ✅ | `5d284ab` | `DOUBAO_API_KEY` 覆盖 yaml 配置 |

**变更文件：**
- `src/buddy_cloud/src/cloud_client_node.cpp` — 重写
- `src/buddy_cloud/include/buddy_cloud/cloud_client_node.hpp` — 重写
- `src/buddy_cloud/CMakeLists.txt` — 添加 CURL、OpenCV
- `src/buddy_app/params/cloud.yaml` — 豆包配置

### 阶段 3：Sherpa-ONNX ASR 集成

| 项目 | 状态 | 提交 | 说明 |
|------|------|------|------|
| 预编译库下载 | ✅ | — | `prebuilt/sherpa-onnx/` (v1.13.0) |
| KWS 模型下载 | ✅ | — | wenetspeech 3.3M 中文唤醒词 |
| ASR 模型下载 | ✅ | — | 流式 zipformer 中英双语 |
| ALSA 音频采集 | ✅ | `4cff181` | 16kHz mono, 100ms chunk |
| KWS 唤醒词检测 | ✅ | `4cff181` | Sherpa-ONNX C API |
| 流式 ASR + endpoint | ✅ | `4cff181` | greedy_search, 自动断句 |
| KWS ↔ ASR 模式切换 | ✅ | `4cff181` | 唤醒 → ASR → 识别完 → 回 KWS |

**变更文件：**
- `src/buddy_audio/include/buddy_audio/audio_pipeline_node.hpp` — 重写
- `src/buddy_audio/src/audio_pipeline_node.cpp` — 重写
- `src/buddy_audio/CMakeLists.txt` — 添加 Sherpa-ONNX、ALSA
- `src/buddy_audio/package.xml` — 添加 libasound2-dev
- `src/buddy_app/params/audio.yaml` — KWS/ASR 模型路径

### 测试覆盖

| 测试 | 数量 | 状态 |
|------|------|------|
| `test_audio_node` (NodeName) | 1 | ✅ |
| `test_brain_node` (lifecycle ×4) | 4 | ✅ |
| `test_segment` (切句 ×4) | 4 | ✅ |
| `test_cloud_node` (lifecycle ×3) | 3 | ✅ |
| **合计** | **12** | **全部通过** |

---

## TODO

### 高优先级

- [ ] **配置 `DOUBAO_API_KEY`** — 设置环境变量启用云端 API 调用
- [ ] **自定义唤醒词** — 当前使用示例 keywords 文件，需替换为 `你好小伙伴` 等自定义词
  - 修改 `audio.yaml` 的 `kws.keywords_file`，或创建自定义 keywords.txt
  - 格式：`n ǐ h ǎo x iǎo h uǒ b àn @你好小伙伴`（拼音 + 标注）
- [ ] **麦克风实测** — 连接物理麦克风验证 KWS → ASR → cloud 全链路
- [ ] **TTS 真实实现** — 当前 `on_sentence` 只打日志，需接入 edge-tts 或其他 TTS 引擎

### 中优先级

- [ ] **情绪主动触发端到端验证** — brain 的 emotion trigger 逻辑已实现，需验证 vision → brain → cloud 链路
- [ ] **buddy_main 参数加载** — 确认 `audio.yaml` 被 buddy_main 正确加载（可能需在启动中添加 `--params-file`）
- [ ] **ASR 静音超时** — 唤醒后长时间无语音应自动回退 KWS（当前依赖 endpoint rule3 的 20s）
- [ ] **错误恢复** — ALSA 设备断开、模型加载失败等异常场景的自动恢复

### 低优先级

- [ ] **第二摄像头支持** — 设计文档已明确本轮不做
- [ ] **ARM 交叉编译** — 下载 aarch64 版 Sherpa-ONNX 预编译库
- [ ] **云端 Provider 切换** — 支持 Gemini 备选（cloud.yaml 已预留 provider 字段）
- [ ] **性能调优** — Sherpa-ONNX num_threads、ALSA buffer 大小、ASR endpoint 灵敏度
- [ ] **集成测试** — 跨包 launch test，验证完整管线

---

## 提交历史

```
4cff181 Add Sherpa-ONNX KWS and streaming ASR to audio pipeline
05a1722 Update docs for 4-package architecture
5b6d3f3 Format all files after architecture simplification
30442f4 Integrate Doubao multimodal API in buddy_cloud
5d284ab Update cloud config for Doubao multimodal API
4b4796b Replace dialog/sentence/state_machine with buddy_brain
a143e22 Add brain node lifecycle and segmentation tests
e0b3c93 Add buddy_brain package with state machine, dialog context, segmentation
e97d1d5 Add CloudRequest message for brain-to-cloud multimodal
3506d45 Revise spec v2: simplify to 4-package architecture
a1f3c54 Add multimodal pipeline upgrade design spec
```

## 当前架构

```
buddy_audio (ALSA + Sherpa-ONNX KWS/ASR + TTS stub)
    ↓ /audio/wake_word, /audio/asr_text
buddy_brain (状态机 + 对话上下文 + 切句 + 情绪仲裁)
    ↓ /brain/cloud_request        ↑ /cloud/response
buddy_cloud (豆包 API, libcurl)
    ↓ CloudChunk 流式返回
buddy_brain → /brain/sentence → buddy_audio (TTS)
buddy_vision (Haar + ONNX 情绪识别) → /vision/emotion
```
