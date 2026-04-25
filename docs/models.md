# 模型准备指南（当前流程）

## 1. 目录约定

模型文件放在仓库根目录 `models/`（默认被 `.gitignore` 忽略）：

- `models/asr/`
- `models/tts/`
- `models/kws/`
- `models/vad/`
- `models/*.rknn`

## 2. 集成原则

1. 本仓库只保存“路径约定”和“参数名”，不提交模型实体。
2. 模型路径通过各包参数 YAML 配置（`src/buddy_app/params/*.yaml`）。
3. 变更模型版本时，必须在 PR 说明兼容性影响。

## 3. 快速检查

```bash
# 检查模型目录（示例）
ls -lah models/

# 构建后验证节点可启动
source /path/to/ros2_core/install/setup.bash
colcon build --packages-select buddy_audio buddy_vision
source install/setup.bash
./output/install/buddy_app/lib/buddy_app/buddy_main
```
