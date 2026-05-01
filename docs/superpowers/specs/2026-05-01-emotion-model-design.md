# Emotion Recognition Model Integration Design

## Goal

Replace `MockModel` in `buddy_vision` with a real ONNX-based emotion recognition pipeline: face detection (YuNet) + expression classification (MobileNet-V2/FER2013), producing 7-class results on x86_64 first, with a clear path to RK3588 (RKNN) deployment.

## Context

- `buddy_vision` already has `ModelInterface` abstraction, dual-thread capture+inference, `EmotionResult.msg`
- Currently uses `MockModel` returning `{"neutral", 0.95}`
- `models/emotion/` directory exists but is empty
- `vision.yaml` already configured with `model_path` and `model_input_width/height: 224`

## Approach

ONNX Runtime dual-model pipeline (Approach A):

1. **Face detection**: YuNet (~2MB ONNX), detects faces + 5 landmarks
2. **Emotion classification**: MobileNet-V2 trained on FER2013 (~5-10MB ONNX), 7-class output

Both models run via ONNX Runtime CPU inference. Both architectures have RKNN conversion support for future RK3588 deployment.

## Dependencies

### ONNX Runtime

- Pre-built tarball managed in `prebuilt/onnxruntime/` (same pattern as ros2_core)
- Source: `onnxruntime-linux-x64-1.21.0.tgz` (already downloaded)
- CMake: `find_path` + `find_library` pointing to `prebuilt/onnxruntime/` (no `find_package`)
- Runtime: `LD_LIBRARY_PATH` includes `prebuilt/onnxruntime/lib`

### Model Files

Managed manually in `models/emotion/`:

```
models/emotion/
├── face_detection.onnx       # YuNet face detector
└── emotion_classifier.onnx   # 7-class emotion classifier
```

## EmotionOnnxModel Design

New class implementing `ModelInterface`:

```
EmotionOnnxModel
├── load(model_dir)
│   ├── Create face_session_ from face_detection.onnx
│   └── Create emotion_session_ from emotion_classifier.onnx
├── inference(frame) -> ModelResult
│   ├── 1. Run face detection on full frame
│   ├── 2. If no face -> return {"no_face", 0.0}
│   ├── 3. Crop largest face, align using landmarks
│   ├── 4. Resize crop to 224x224
│   └── 5. Run emotion classification -> return top class
└── unload()
    ├── Delete face_session_
    └── Delete emotion_session_
```

### Emotion Label Mapping

Hardcoded index-to-label:

```
0: angry, 1: disgust, 2: fear, 3: happy, 4: sad, 5: surprise, 6: neutral
```

Actual mapping will match the specific model's training label order.

## File Changes

| File | Action | Description |
|------|--------|-------------|
| `buddy_vision/include/buddy_vision/onnx_emotion_model.hpp` | New | EmotionOnnxModel header |
| `buddy_vision/src/onnx_emotion_model.cpp` | New | EmotionOnnxModel implementation |
| `buddy_vision/src/vision_pipeline_node.cpp` | Modify | Swap MockModel -> EmotionOnnxModel |
| `buddy_vision/CMakeLists.txt` | Modify | Add onnxruntime include/lib |
| `buddy_vision/package.xml` | Modify | Add onnxruntime dependency |
| `buddy_app/params/vision.yaml` | Modify | Add face_detection_model_path param |
| `build.sh` | Modify | Add onnxruntime to LD_LIBRARY_PATH |

## Unchanged

- `buddy_interfaces`: `EmotionResult.msg` already sufficient
- `VisionPipelineNode` architecture: only swaps `ModelInterface` implementation
- Existing tests: still use `MockModel`
- `ModelInterface` abstract interface: no changes needed

## Future: RK3588 Deployment

- Both YuNet and MobileNet-V2 are supported by RKNN Toolkit 2
- Will need `EmotionRknnModel` class (same `ModelInterface`, different backend)
- Model conversion: ONNX -> RKNN via rknn-toolkit2
- Selected via config param or build flag
