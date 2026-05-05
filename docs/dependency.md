# Prebuilt 依赖安装指南

`prebuilt/` 存放编译时依赖的二进制库，不纳入版本库。

## 1. 依赖关系

```
prebuilt/
  ├── ros2_core/      ROS 2 Humble 核心（rclcpp, lifecycle, components 等）
  ├── onnxruntime/    ONNX 模型通用推理引擎
  └── sherpa-onnx/    语音推理框架（内部链接 onnxruntime）
```

**onnxruntime vs sherpa-onnx：**

| | onnxruntime | sherpa-onnx |
|---|---|---|
| 本质 | 通用 ONNX 推理引擎 | 语音处理框架（基于 onnxruntime） |
| 用途 | 直接加载 .onnx 模型推理 | 提供 ASR/KWS/TTS/VAD 高层 API |
| 使用者 | `buddy_vision` | `buddy_audio` |
| 依赖 | 无 | 内部打包了 onnxruntime.so |

关系：`sherpa-onnx` 内部链接了 `onnxruntime`，两者各自带了一份 `libonnxruntime.so`。

## 2. 安装方法

### 2.1 ROS 2 Core

```bash
mkdir -p prebuilt/ros2_core
tar xzf prebuilt/ros2-humble-x86_64.tar.gz -C prebuilt/ros2_core/
```

> 来源：`ros2_core` 仓库通过 CI 构建产出 tarball。

### 2.2 ONNX Runtime

`buddy_vision` 编译和运行依赖。

```bash
cd prebuilt
wget https://github.com/microsoft/onnxruntime/releases/download/v1.21.0/onnxruntime-linux-x64-1.21.0.tgz
tar xzf onnxruntime-linux-x64-1.21.0.tgz
mv onnxruntime-linux-x64-1.21.0 onnxruntime
rm onnxruntime-linux-x64-1.21.0.tgz
```

> 下载地址：https://github.com/microsoft/onnxruntime/releases
> 版本：1.21.0（与 sherpa-onnx 打包的版本一致）

### 2.3 Sherpa-ONNX

`buddy_audio` 编译和运行依赖。

```bash
cd prebuilt
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/sherpa-onnx-1.10.30/sherpa-onnx-v1.10.30-linux-x64-shared.tar.bz2
tar xjf sherpa-onnx-v1.10.30-linux-x64-shared.tar.bz2
mv sherpa-onnx-v1.10.30-linux-x64-shared sherpa-onnx
rm sherpa-onnx-v1.10.30-linux-x64-shared.tar.bz2
```

> 下载地址：https://github.com/k2-fsa/sherpa-onnx/releases
> 选择 `linux-x64-shared` 版本（只要 .so，不需要 Python wheel）
> 注意版本号需要与 audio.yaml 中模型版本匹配

## 3. 验证安装

```bash
# 检查目录结构
ls prebuilt/ros2_core/setup.bash
ls prebuilt/onnxruntime/lib/libonnxruntime.so
ls prebuilt/sherpa-onnx/lib/libsherpa-onnx-c-api.so

# 构建
./build.sh

# 运行时确保 LD_LIBRARY_PATH 包含两个库路径
# build.sh 会自动处理
```
