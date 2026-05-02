# Buddy Robot 架构文档（ROS 2 组件化）

版本: v6.0
日期: 2026-05-03
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

主流程（双脑架构）：

```
Audio → Brain → Vision (optional) → Local LLM + Cloud → Brain → Audio playback
                                                  ↑
                                          双模型并行推理
                                    本地先回复，云端替换
```

1. buddy_audio — 唤醒词检测、ASR、TTS回放
2. buddy_brain — 状态机、对话上下文、切句、双流合并
3. buddy_vision — 图像采集与情感识别
4. buddy_cloud — 豆包API多模态请求（云端大模型）
5. buddy_local_llm — ollama本地推理（快速初始回复）
6. buddy_brain — 本地回复先播，云端到了替换 → audio TTS

## 4. 依赖策略

1. 本仓库不再使用 Conan。
2. ROS2 基础依赖来自预编译 tarball，解压到 `prebuilt/` 后由 `build.sh` 自动 source。
3. 第三方模型文件（`.onnx`、`.rknn`）不纳入版本库。
4. 本地 LLM 推理依赖外部 ollama 服务（默认 `localhost:11434`）。