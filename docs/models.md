# 模型准备指南

## 1. 目录约定

模型文件放在各模块的 `models/` 目录下（被 `.gitignore` 忽略）：

```
src/buddy_audio/models/
  ├── sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/   # ASR
  └── sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile/ # KWS

src/buddy_vision/models/
  ├── emotion/   # face_detection.onnx, emotion_classifier.onnx
  └── game/      # 游戏相关模型
```

## 2. Audio 模型下载

```bash
cd src/buddy_audio/models

# ASR 模型 — 中英双语流式语音识别 (~487MB)
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20.tar.bz2
tar xjf sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20.tar.bz2
rm sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20.tar.bz2

# KWS 模型 — 中文关键词唤醒
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile.tar.bz2
tar xjf sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile.tar.bz2
rm sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile.tar.bz2
```

## 3. 集成原则

1. 本仓库只保存路径约定和参数名，不提交模型实体。
2. 模型路径通过各包参数 YAML 配置（`src/buddy_app/params/*.yaml`）。
3. 变更模型版本时，必须在 PR 说明兼容性影响。

## 4. 快速检查

```bash
# 检查模型目录
ls src/buddy_audio/models/
ls src/buddy_vision/models/

# 构建 & 验证
./build.sh
source output/install/setup.bash
./output/install/buddy_app/lib/buddy_app/buddy_main
```
