# Buddy Robot 架构文档（ROS 2 组件化）

版本: v5.0  
日期: 2026-04-30  
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
- `src/buddy_cloud`：云端请求与流式回包
- `src/buddy_state_machine`：流程编排
- `src/buddy_dialog`：对话管理
- `src/buddy_sentence`：切句
- `src/buddy_app`：C++ 入口程序，加载所有组件，包含参数配置

## 3. 运行拓扑

启动后组件运行在同一个进程内（`buddy_main`），通过 class_loader 动态加载组件 .so，开启 intra-process 通信。

主链路（简化）：

1. Audio/Dialog 触发请求
2. StateMachine 组织上下文
3. Vision 提供图像结果（可选）
4. Cloud 返回流式文本
5. Sentence 切句
6. Audio 播放并回执

## 4. 依赖策略

1. 本仓库不再使用 Conan。
2. ROS2 基础依赖来自预编译 tarball，解压到 `prebuilt/` 后由 `build.sh` 自动 source。
3. 第三方模型文件（`.onnx`、`.rknn`）不纳入版本库。
