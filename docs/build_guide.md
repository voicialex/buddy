# 编译流程指南

本文档说明 buddy_robot 的构建架构和常用操作。

---

## 1. 构建架构：Underlay + Overlay

项目分两层，Underlay 预编译下载，Overlay 本地编译：

```text
┌─────────────────────────────────────────────────────┐
│  Overlay: src/buddy_robot/                          │
│  8 个业务包（buddy_audio, buddy_vision 等）          │
│  依赖 Underlay 提供的 rclcpp, std_msgs 等           │
├─────────────────────────────────────────────────────┤
│  Underlay: third_party/ros2/{humble|jazzy}/         │
│  ROS2 核心库（从 GitHub Release 预编译下载）         │
│  ~80 个包（rclcpp, rosidl, rmw, DDS 等）            │
│  脚本根据 Ubuntu 版本自动选择:                       │
│    Ubuntu 22.04 → humble                            │
│    Ubuntu 24.04 → jazzy                             │
├─────────────────────────────────────────────────────┤
│  系统层: apt 提供的基础库                            │
│  cmake, g++, libssl-dev, libtinyxml2-dev 等          │
└─────────────────────────────────────────────────────┘
```

**为什么要分两层？**

1. Underlay 是 ROS2 框架本身，变化少，下载一次后很少需要更新。
2. Overlay 是业务代码，开发时频繁修改，单独编译只需 ~48 秒。
3. 分开后，Overlay 编译不需要重新编译整个 ROS2。

---

## 2. ros2_core 与版本管理

Underlay 预编译包来自 [ros2_core](https://github.com/voicialex/ros2_core) 仓库的 GitHub Release。

版本通过项目根目录的 `.ros_core_version` 文件锁定：

```text
.ros_core_version    # 内容如: v2026.04.1
```

`setup` 命令会读取此文件，下载对应的 `ros2-{humble|jazzy}-{arch}.tar.gz` 到 `third_party/ros2/{humble|jazzy}/install/`。

升级版本时只需修改 `.ros_core_version` 的内容，然后重新运行 `setup`。

---

## 3. 构建命令

### 一键构建

```bash
./scripts/build_all.sh            # setup + build + test
```

### 分步执行

```bash
./scripts/build_all.sh setup   # 第一步：下载预编译 Underlay（~30s）
./scripts/build_all.sh build   # 第二步：编译 Overlay（~48s）
./scripts/build_all.sh test    # 第三步：运行测试
```

### 每一步在做什么

#### 第一步：setup（~30 秒）

```text
读取 .ros_core_version 版本号
        ↓
从 GitHub Release 下载 ros2-{distro}-{arch}.tar.gz
        ↓
解压到 third_party/ros2/{humble|jazzy}/install/
```

产物目录（以 jazzy 为例，humble 结构相同）：

```text
third_party/ros2/jazzy/
└── install/                    # [gitignore] 预编译产物 (~49MB)
    └── setup.bash              #   ← source 这个就能用
```

#### 第二步：build（~48 秒）

```text
① source third_party/ros2/{humble|jazzy}/install/setup.bash
   加载 underlay，让 colcon 找到 rclcpp 等依赖
        ↓
② cd src/buddy_robot
   colcon build
   编译 8 个业务包（buddy_audio, buddy_vision 等）
   产物 → src/buddy_robot/install/  (~2.6MB)
```

colcon 编译顺序（按依赖拓扑排列）：

```text
colcon build
    ├── buddy_interfaces  (最先编译，其他包依赖它的消息定义)
    ├── buddy_audio       (等 buddy_interfaces 编完)
    ├── buddy_vision
    ├── buddy_cloud
    ├── buddy_dialog
    ├── buddy_sentence
    ├── buddy_state_machine
    └── buddy_bringup     (最后，它依赖上面所有包)
```

#### 第三步：test（~1 秒）

```text
source underlay + overlay
colcon test → 26 个测试全部通过
```

---

## 4. 运行

```bash
# 必须先 source underlay，再 source overlay，顺序不能反
source third_party/ros2/{humble|jazzy}/install/setup.bash
source src/buddy_robot/install/setup.bash
ros2 launch buddy_bringup buddy.launch.py
```

---

## 5. 常见操作

### 只修改了业务代码，重新编译

```bash
./scripts/build_all.sh build    # 只编 overlay，~48 秒
```

### 修改了 buddy_interfaces 的消息定义

```bash
./scripts/build_all.sh build    # colcon 会自动检测依赖关系，先编 interfaces 再编其他
```

### 更新 ROS2 Underlay 版本

```bash
# 修改 .ros_core_version 为新版本号（如 v2026.05.1）
./scripts/build_all.sh setup    # 重新下载
```

### 清理构建产物

```bash
./scripts/build_all.sh clean    # 删除 overlay 的 build/ install/ log/
```

### 完全从零开始

```bash
./scripts/build_all.sh clean
./scripts/build_all.sh          # setup + build + test
```

---

## 6. 换台机器怎么办

```bash
git clone <repo-url>
cd buddy_robot

# 前置工具（不需要 vcs、rosdep）
sudo apt install -y build-essential cmake git \
    python3-colcon-common-extensions \
    libssl-dev libtinyxml2-dev libcurl4-openssl-dev

# 一键构建（下载 underlay + 编译 overlay + 测试）
./scripts/build_all.sh
```

`.ros_core_version` 保证了每台机器下载到的 ROS2 版本完全一致。

---

## 7. 双版本支持（Humble + Jazzy）

项目同时支持 ROS2 Humble (Ubuntu 22.04) 和 Jazzy (Ubuntu 24.04)，C++ 代码零修改即可在两个版本上编译运行。

### 自动检测

`build_all.sh` 启动时自动读取 `/etc/os-release` 判断 Ubuntu 版本：

```text
Ubuntu 22.04 → 下载 ros2-humble-{arch}.tar.gz
Ubuntu 24.04 → 下载 ros2-jazzy-{arch}.tar.gz
```

脚本会打印提示：`[INFO] 检测到 Ubuntu 24.04 → ROS2 JAZZY`

### 目录结构

```text
third_party/ros2/
├── humble/                    # Ubuntu 22.04 用
│   └── install/              #   预编译 underlay（下载）
└── jazzy/                     # Ubuntu 24.04 用
    └── install/              #   预编译 underlay（下载）
```

### 清理

`./scripts/build_all.sh clean` 清理 overlay 的构建产物（build/ install/ log/）。
