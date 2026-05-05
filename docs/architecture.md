# Buddy Robot 架构文档（ROS 2 组件化）

版本: v7.0
日期: 2026-05-05
状态: 当前有效

## 1. 架构结论

项目采用 ROS 2 组件化单容器部署：

1. 所有业务模块以 `rclcpp_components` 方式实现。
2. 通过 `buddy_app/src/buddy_main.cpp` 使用 class_loader 加载所有组件到单进程。
3. 模块间通信使用 ROS 2 Topic/Service，自定义协议在 `buddy_interfaces` 中定义。
4. 参数配置放在 `buddy_app/params/*.yaml`。

## 2. 代码结构

- `src/buddy_interfaces`：消息与服务定义
- `src/buddy_audio`：音频入口与 TTS 回执
- `src/buddy_vision`：视觉处理链路（详见 [vision_architecture.md](vision_architecture.md)）
- `src/buddy_cloud`：云端推理请求（Doubao API）
- `src/buddy_local_llm`：本地推理请求（ollama + Gemma 4 E2B）
- `src/buddy_brain`：中央编排（状态机 + 对话上下文 + 切句 + 双流合并）
- `src/buddy_app`：C++ 入口程序，加载所有组件，包含参数配置

## 3. 运行拓扑

启动后组件运行在同一个进程内（`buddy_main`），通过 class_loader 动态加载组件 .so，开启 intra-process 通信。

### 3.1 模块架构图

```
                              Topic 流向总览
  ┌──────────┐  wake_word   ┌──────────┐  /brain/request  ┌───────────────┐
  │          │  asr_text    │          │────────────────▶  │buddy_local_llm│
  │          │────────────▶ │          │                   │   (ollama)    │
  │          │              │  buddy   │◀─ local_chunk ─── │               │
  │  buddy   │  sentence    │  brain   │                   └───────────────┘
  │  _audio  │◀──────────── │          │  /brain/request   ┌───────────────┐
  │          │              │          │────────────────▶  │ buddy_cloud   │
  │          │  tts_done    │          │                   │ (Doubao API)  │
  │          │────────────▶ │          │◀─ cloud_chunk ── │               │
  └──────────┘              └────┬─────┘                   └───────────────┘
                                 ▲
                    emotion_result│  srv: capture_image
                                 │
                            ┌────┴─────┐
                            │  buddy   │
                            │ _vision  │
                            └──────────┘

  buddy_brain 是唯一中枢:
    audio  ◀──▶  brain  ──▶  local_llm (chunk 回流到 brain)
    vision ──▶   brain  ──▶  cloud     (chunk 回流到 brain)

时序说明 (Dual-Brain):

  1. audio KWS检测唤醒词  ──▶ brain LISTENING
  2. audio ASR识别语音    ──▶ brain REQUESTING ──▶ local_llm + cloud 并行推理
  3. local_llm 先返回      ──▶ brain 分句 ──▶ audio TTS播放 (低延迟)
  4. cloud 到达后          ──▶ brain 替换local ──▶ audio 播放云端回复 (高质量)
  5. audio tts_done        ──▶ brain IDLE
```

## 4. 依赖策略

2. ROS2 基础依赖来自预编译 tarball，解压到 `prebuilt/` 后由 `build.sh` 自动 source。
3. 第三方模型文件（`.onnx`、`.rknn`）不纳入版本库。
4. 本地 LLM 推理依赖外部 ollama 服务（默认 `localhost:11434`）。
