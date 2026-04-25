# 模型准备指南

## 1. 语音模型（sherpa-onnx）

### 1.1 编译 sherpa-onnx（含 RKNN NPU 后端）

```bash
git clone https://github.com/k2-fsa/sherpa-onnx
cd sherpa-onnx

# RK3588 板端编译（启用 RKNN 后端）
chmod +x ./build-scripts/build-rknn-linux-aarch64.sh
./build-scripts/build-rknn-linux-aarch64.sh

# 安装到 third_party/
mkdir -p ../third_party/sherpa-onnx/lib ../third_party/sherpa-onnx/include
cp build/lib/libsherpa-onnx-core.so ../third_party/sherpa-onnx/lib/
cp -r sherpa-onnx/c-api/ ../third_party/sherpa-onnx/include/sherpa-onnx/
```

### 1.2 下载 ASR 模型（Zipformer 流式，中文）

```bash
# 推荐: sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20
cd models/asr/
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/\
sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20.tar.bz2
tar xf *.tar.bz2
mv sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/* .
rm -rf sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20 *.tar.bz2
```

`buddy_bringup/params/buddy_params.yaml` 中 `audio.asr_model_path` 设为 `"/opt/models/sherpa_onnx/"`

### 1.3 下载 TTS 模型（Piper，中文）

```bash
cd models/tts/
# 中文女声（推荐）
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/\
vits-zh-aishell3.tar.bz2
tar xf *.tar.bz2 && rm *.tar.bz2
```

### 1.4 下载 KWS 模型（自定义唤醒词）

```bash
cd models/kws/
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/\
sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01.tar.bz2
tar xf *.tar.bz2 && rm *.tar.bz2
```

### 1.5 下载 VAD 模型（Silero VAD）

```bash
cd models/vad/
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/\
silero_vad.onnx
```

---

## 2. 视觉模型（RKNN）

### 2.1 准备工作（在 x86 PC 上执行）

```bash
pip install rknn-toolkit2  # 需要 Python 3.8-3.12

# 克隆 RKNN Model Zoo
git clone https://github.com/airockchip/rknn_model_zoo
```

### 2.2 人脸检测模型（RetinaFace）

```bash
# 下载预转换的 RKNN 模型（推荐，省去转换步骤）
cd models/
wget https://github.com/airockchip/rknn_model_zoo/releases/download/v2.3.0/\
retinaface_mobile320.rknn -O face_det.rknn

# 或者自行转换
# 参考: rknn_model_zoo/examples/RetinaFace/python/convert.py
```

### 2.3 表情识别模型（FER+）

```bash
# 从 ONNX Model Zoo 获取 FER+ 模型
pip install onnxruntime
wget https://github.com/onnx/models/raw/main/validated/vision/body_analysis/\
emotion_ferplus/model/emotion-ferplus-8.onnx

# 转换为 RKNN（在 PC 上）
python3 - << 'EOF'
from rknn.api import RKNN

rknn = RKNN()
rknn.config(mean_values=[[0, 0, 0]], std_values=[[255, 255, 255]],
            target_platform='rk3588')
rknn.load_onnx(model='emotion-ferplus-8.onnx')
rknn.build(do_quantization=True, dataset='calibration_images.txt')
rknn.export_rknn('emotion.rknn')
rknn.release()
EOF

cp emotion.rknn models/emotion.rknn
```

**量化校准数据集**：准备 100 张人脸图片路径列表保存为 `calibration_images.txt`。

### 2.4 验证 RKNN 环境（板端）

```bash
# 检查 NPU 驱动
ls /dev/rknpu*     # 应看到 /dev/rknpu0 等

# 测试 RKNN Runtime
python3 -c "import rknnlite.api; print('RKNN OK')"

# 或使用 C API
# 参考: rknn_model_zoo/examples/RetinaFace/cpp/main.cc
```

---

## 3. 目录结构

转换完成后，`models/` 目录结构应为：

```
models/
├── face_det.rknn            # 人脸检测模型
├── emotion.rknn             # 表情识别模型
├── asr/                     # ASR 模型目录
│   ├── tokens.txt
│   ├── encoder-epoch-99-avg-1.onnx
│   ├── decoder-epoch-99-avg-1.onnx
│   └── joiner-epoch-99-avg-1.onnx
├── tts/                     # TTS 模型目录
│   ├── vits-aishell3.onnx
│   └── lexicon.txt
├── kws/                     # 唤醒词模型目录
│   ├── encoder.onnx
│   ├── decoder.onnx
│   └── tokens.txt
└── vad/
    └── silero_vad.onnx      # VAD 模型
```

---

## 4. 模型性能参考（RK3588）

| 模型 | 精度 | FPS / RTF | NPU 核心 |
|------|------|-----------|---------|
| RetinaFace (320) | INT8 | ~470 FPS | Core 1 |
| FER+ (48x48) | INT8 | ~200 FPS | Core 1 |
| Zipformer ASR (流式) | FP16 | RTF ~0.08 | Core 0 |
| Piper TTS | FP16 | RTF ~0.07 | Core 0 |
| Silero VAD | FP32 | RTF ~0.01 | CPU |
