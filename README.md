# Buddy Robot (ROS 2 Workspace)

基于 ROS 2 组件化架构的 Buddy 机器人项目。当前代码以 `ament_cmake` 包组织，运行方式是 `ComposableNodeContainer` 组合节点。

## 项目结构

- `src/`：主工作区源码（`buddy_*` 包）
- `src/buddy_interfaces`：自定义消息与服务
- `src/buddy_app`：C++ 入口程序与参数配置
- `docs/`：设计与迁移文档
- `prebuilt/`：预编译 ROS 2 Core 依赖（解压 tarball，不提交）
- `output/`：编译输出（build、install、log，不提交）

## 构建与运行

前置约定：将 ros2_core 的 tarball 解压到 `prebuilt/` 下。

```bash
# 解压 ROS 2 依赖
tar xzf ros2-humble-x86_64.tar.gz -C prebuilt/

# 一键构建
./build.sh

# 可选：传递 colcon 参数
./build.sh --packages-select buddy_audio buddy_state_machine

# 加载本工作区 overlay
source output/install/setup.bash

# 启动
./output/install/buddy_app/lib/buddy_app/buddy_main
```

## 测试

```bash
# 运行指定包测试
colcon test --packages-select buddy_audio buddy_vision

# 查看测试结果
colcon test-result --verbose
```

## 依赖说明

- 依赖来源以 ROS 2 Core（预编译 tarball）+ 系统库为主。
- 模型文件（如 `.onnx`、`.rknn`）不入库。
