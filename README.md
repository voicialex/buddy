# Buddy Robot — RK3588 AI 陪伴机器人

基于 RK3588 的边缘端 AI 陪伴机器人，使用 ROS2 Jazzy LifecycleNode 架构，本地运行语音交互（唤醒/ASR/TTS）和视觉感知（人脸检测/表情识别），通过触发条件联动云端大模型。

## 项目概述

- **硬件平台**: RK3588（Orange Pi 5 Plus / Firefly ROC-RK3588S-PC / Radxa Rock 5B，≥8GB RAM）
- **操作系统**: Ubuntu 22.04 / 24.04 LTS (aarch64)
- **ROS 发行版**: Humble (22.04) / Jazzy (24.04)（预编译下载，自动检测）
- **开发语言**: C++17
- **构建工具**: colcon + ament_cmake

## 核心特性

| 特性 | 说明 |
|------|------|
| 唤醒词检测 | 离线，Sherpa-ONNX KWS，< 200ms |
| 流式语音识别 | 离线，Sherpa-ONNX Zipformer，RTF < 0.1 |
| 本地语音合成 | 离线，Sherpa-ONNX Piper/VITS |
| 人脸检测 | RKNN NPU，RetinaFace，≥ 15 FPS |
| 表情识别 | RKNN NPU，7 类情绪 |
| 云端对话 | 豆包 Ark（主链路）+ OpenAI 兼容 API |
| 首响延迟 | 唤醒 → 听到回复 < 2 秒 |
| 架构形态 | ROS2 LifecycleNode + colcon 工作空间 |

## 快速开始

### 前置条件

```bash
# 构建工具（仅需这些系统包）
sudo apt install -y build-essential cmake git python3-colcon-common-extensions \
    libssl-dev libtinyxml2-dev libcurl4-openssl-dev
```

ROS2 核心通过 [ros2_core](https://github.com/voicialex/ros2_core) 下载预编译包，**不需要** `apt install ros-*`、`vcs`、`rosdep`。
构建脚本会根据 Ubuntu 版本自动选择：22.04 → Humble，24.04 → Jazzy。

### 构建（Underlay + Overlay）

项目采用 Underlay + Overlay 分层架构，Underlay 为预编译下载：

```text
third_party/ros2/{humble|jazzy}/install/   ← Underlay：ROS2 核心库（从 GitHub Release 下载）
              ↓ source
src/buddy_robot/install/           ← Overlay：buddy_robot 8 包（~2.6MB）
```

#### 一键构建

```bash
./scripts/build_all.sh            # 下载 underlay + 编译 overlay + 测试
```

#### 分步构建

```bash
# 第一步：下载预编译 Underlay（ROS2 核心，从 GitHub Release 下载）
./scripts/build_all.sh setup

# 第二步：编译 Overlay（buddy_robot 包，~48s）
./scripts/build_all.sh build

# 第三步：运行测试
./scripts/build_all.sh test
```

#### 手动构建（等价于脚本）

```bash
# Overlay（需先运行 setup 下载 underlay）
cd src/buddy_robot
source ../../third_party/ros2/$ROS_DISTRO/install/setup.bash
colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Release
```

### 运行

```bash
source third_party/ros2/$ROS_DISTRO/install/setup.bash   # 先加载 underlay (jazzy 或 humble)
source src/buddy_robot/install/setup.bash           # 再加载 overlay
ros2 launch buddy_bringup buddy.launch.py           # 任意目录均可
```

### 运行测试

```bash
source third_party/ros2/$ROS_DISTRO/install/setup.bash
source src/buddy_robot/install/setup.bash
cd src/buddy_robot                                    # 必须进入 overlay 目录
colcon test --event-handlers console_cohesion+
# 或单独测试
colcon test --packages-select buddy_sentence
```

### 清理

```bash
./scripts/build_all.sh clean    # 删除所有构建产物
```

## 产物目录

| 目录 | 内容 | 大小 | git |
|------|------|------|-----|
| `third_party/ros2/{humble\|jazzy}/install/` | Underlay 预编译产物（~80 包，从 GitHub Release 下载） | ~49MB | 忽略 |
| `src/buddy_robot/build/` | Overlay 编译中间文件 | — | 忽略 |
| `src/buddy_robot/install/` | Overlay 安装产物（8 包） | ~2.6MB | 忽略 |
| `src/buddy_robot/log/` | Overlay 编译日志 | — | 忽略 |

## 项目结构

```text
buddy_robot/
├── README.md
├── CLAUDE.md                       # Claude Code 指令
├── .ros_core_version               # ros2_core 版本号（如 v2026.04.1）
├── scripts/
│   ├── build_all.sh                # Overlay 构建脚本（自动检测 Ubuntu 版本）
│   └── setup_underlay.sh           # 下载预编译 ROS2 underlay
├── third_party/
│   └── ros2/
│       ├── humble/                 # ROS2 Humble (Ubuntu 22.04)
│       │   └── install/            #   预编译 underlay（从 GitHub Release 下载）
│       └── jazzy/                  # ROS2 Jazzy (Ubuntu 24.04)
│           └── install/            #   预编译 underlay（从 GitHub Release 下载）
├── src/buddy_robot/                # buddy_robot 工作空间（Overlay）
│   ├── buddy_audio/                # 音频流水线 LifecycleNode
│   ├── buddy_vision/               # 视觉流水线 LifecycleNode
│   ├── buddy_cloud/                # 云端客户端 LifecycleNode
│   ├── buddy_dialog/               # 对话管理 LifecycleNode
│   ├── buddy_sentence/             # 分句器 LifecycleNode
│   ├── buddy_state_machine/        # 状态机编排 LifecycleNode
│   ├── buddy_interfaces/           # 自定义 msg/srv 定义
│   ├── buddy_bringup/              # launch 文件 + 参数配置
│   └── install/                    # Overlay 编译产物（setup.bash）
├── docs/                           # 设计文档
└── models/                         # 模型文件（.gitignore）
```

## 文档索引

- **[编译流程指南](docs/build_guide.md)** — Underlay + Overlay 构建架构、构建命令、常见操作
- **[软件架构设计](docs/architecture.md)** — ROS2 LifecycleNode 架构、包间通信、状态机编排
- **[通信协议规范](docs/communication_protocol.md)** — ROS2 Topic/Service 定义、云端 WebSocket 协议
- **[实施计划总览](docs/plan.md)** — 分阶段实施计划、里程碑、已实现与待办
- **[模型准备指南](docs/models.md)** — 模型下载、转换、配置
- **[方案对比记录](docs/compare_custom_vs_ros2.md)** — 自研 EventBus vs ROS2 迁移决策记录

## 依赖库

| 库 | 版本 | 用途 | 来源 |
|---|---|---|---|
| ROS2 Humble / Jazzy | 2022 / 2024 | 框架核心 | ros2_core 预编译下载 |
| RKNN Runtime (librknnrt) | ≥ 1.6 | NPU 推理 | 手动安装 |
| [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | ≥ 1.10 | ASR/TTS/VAD/KWS | 手动编译 |
| OpenCV | ≥ 4.5 | 图像处理 | apt |
| libcurl | ≥ 7.80 | HTTP/SSE 云端通信 | apt |
| speexdsp | ≥ 1.2 | 回声消除（AEC） | apt |

## 云端 LLM 支持

框架主链路使用豆包 Ark，接口风格按 OpenAI 兼容组织，请求参数通过 `buddy_params.yaml` 配置。

| 供应商 | base_url | 特点 |
|--------|---------|------|
| 豆包 Ark | `https://ark.cn-beijing.volces.com/api/v3` | 主链路，推荐 |
| DeepSeek | `https://api.deepseek.com/v1` | 备选 |
| 通义千问 | `https://dashscope.aliyuncs.com/compatible-mode/v1` | 备选 |
| OpenAI | `https://api.openai.com/v1` | 备选 |
