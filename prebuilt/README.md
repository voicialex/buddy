# prebuilt — Pre-built Dependencies

## ROS 2 Core

```bash
mkdir -p prebuilt/ros2_core
tar xzf prebuilt/ros2-humble-x86_64.tar.gz -C prebuilt/ros2_core/
```

`build.sh` will auto-source `prebuilt/ros2_core/setup.bash`.

## ONNX Runtime

```bash
tar xzf prebuilt/onnxruntime-linux-x64-1.21.0.tgz -C prebuilt/
mv prebuilt/onnxruntime-linux-x64-1.21.0 prebuilt/onnxruntime
```

`build.sh` will add `prebuilt/onnxruntime` to `CMAKE_PREFIX_PATH`.

Contents are gitignored — only this README and tarballs are tracked.
