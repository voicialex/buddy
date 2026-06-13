#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARCH="$(uname -m)"
INSTALL_DIR="$ROOT_DIR/output/${ARCH}/install"
SETUP="$INSTALL_DIR/setup.bash"
PREBUILT_CURRENT="$ROOT_DIR/prebuilt/current"


# --- Check video group membership for camera access ---
if ! groups | grep -qw video; then
  echo "[ERROR] Current user is NOT in the 'video' group — camera controls will fail." >&2
  echo "[HINT]  Run: sudo usermod -aG video \$USER && newgrp video" >&2
  exit 1
fi

if [[ ! -f "$SETUP" ]]; then
  echo "[ERROR] Workspace not built yet: $SETUP not found" >&2
  echo "[HINT]  Run ./build.sh first" >&2
  exit 1
fi

BUDDY_MAIN="$INSTALL_DIR/buddy_app/lib/buddy_app/buddy_main"
if [[ ! -x "$BUDDY_MAIN" ]]; then
  echo "[ERROR] buddy_main not found: $BUDDY_MAIN" >&2
  exit 1
fi

# --- Check for existing buddy_main process ---
RUNNING_PIDS=$(pgrep -f "$BUDDY_MAIN" 2>/dev/null || true)
if [[ -n "$RUNNING_PIDS" ]]; then
  echo "[WARN] buddy_main is already running (PID: $(echo "$RUNNING_PIDS" | tr '\n' ' '))" >&2
  read -rp "[WARN] Kill existing process and restart? [y/N] " answer
  case "$answer" in
    [yY]|[yY][eE][sS])
      echo "[INFO] Killing PID(s): $RUNNING_PIDS"
      echo "$RUNNING_PIDS" | xargs kill
      # Wait briefly for graceful shutdown
      for i in 1 2 3 4 5; do
        RUNNING_PIDS=$(pgrep -f "$BUDDY_MAIN" 2>/dev/null || true)
        [[ -z "$RUNNING_PIDS" ]] && break
        sleep 1
      done
      # Force kill if still alive
      RUNNING_PIDS=$(pgrep -f "$BUDDY_MAIN" 2>/dev/null || true)
      if [[ -n "$RUNNING_PIDS" ]]; then
        echo "[WARN] Process did not exit gracefully, force killing..." >&2
        echo "$RUNNING_PIDS" | xargs kill -9
        sleep 1
      fi
      echo "[INFO] Existing process terminated."
      ;;
    *)
      echo "[INFO] Aborted." >&2
      exit 0
      ;;
  esac
fi

set +u
# shellcheck disable=SC1090
source "$SETUP"
set -u

# --- Runtime shared libraries (prebuilt deps) ---
ORT_LIB_DIR="onnxruntime"
if [[ -d "$PREBUILT_CURRENT/onnxruntime-gpu/lib" ]]; then
  ORT_LIB_DIR="onnxruntime-gpu"
fi

LIB_PATHS=()
for d in \
  "$PREBUILT_CURRENT/opencv/lib" \
  "$PREBUILT_CURRENT/$ORT_LIB_DIR/lib" \
  "$PREBUILT_CURRENT/sherpa-onnx/lib" \
  "$PREBUILT_CURRENT/rknn/lib"
do
  [[ -d "$d" ]] && LIB_PATHS+=("$d")
done
if [[ ${#LIB_PATHS[@]} -gt 0 ]]; then
  export LD_LIBRARY_PATH="$(IFS=:; echo "${LIB_PATHS[*]}"):${LD_LIBRARY_PATH:-}"
fi

# --- Unified module status (single source: scripts/status.sh) ---
if [[ -x "$ROOT_DIR/scripts/status.sh" ]]; then
  "$ROOT_DIR/scripts/status.sh" --module || true
fi

# --- Check critical models ---
PARAMS_DIR="$INSTALL_DIR/buddy_app/share/buddy_app/params"
ASR_YAML="$PARAMS_DIR/audio.asr.yaml"
TTS_YAML="$PARAMS_DIR/audio.tts.yaml"
MODULES_YAML="$PARAMS_DIR/modules.yaml"
MODELS_DIR="$ROOT_DIR/models"
MODELS_MISSING=()
ASR_MODE=$(grep -A2 "^    asr:" "$ASR_YAML" | grep "mode:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")
TTS_MODE=$(grep -A2 "^    tts:" "$TTS_YAML" | grep "mode:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")
TTS_ENGINE=$(grep -A4 "^    tts:" "$TTS_YAML" | grep "engine:" | head -1 | awk '{print $2}' | tr -d '"' || echo "auto")
TTS_RUNTIME=$(grep -A4 "^    tts:" "$TTS_YAML" | grep "runtime:" | head -1 | awk '{print $2}' | tr -d '"' || echo "auto")
KWS_ENABLE=$(grep -A2 "^    kws:" "$ASR_YAML" | grep "enable:" | head -1 | awk '{print $2}' | tr -d '"' || echo "false")
VISION_ENABLE=$(grep -A10 "^modules:" "$MODULES_YAML" 2>/dev/null | grep "vision:" | head -1 | awk '{print $2}' | tr -d '"' || echo "true")

# ASR local mode: accept either sherpa (ONNX) or native (RKNN) model set
if [[ "$ASR_MODE" == "local" ]]; then
  SHERPA_TOKENS="$MODELS_DIR/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/tokens.txt"
  NATIVE_TOKENS="$MODELS_DIR/zipformer-rknn/tokens.txt"
  if [[ ! -f "$SHERPA_TOKENS" && ! -f "$NATIVE_TOKENS" ]]; then
    MODELS_MISSING+=("ASR (need sherpa-onnx or zipformer-rknn model)")
  fi
fi

# KWS only when enabled
if [[ "$KWS_ENABLE" == "true" ]]; then
  [[ ! -f "$MODELS_DIR/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile/tokens.txt" ]] && \
    MODELS_MISSING+=("KWS (keyword spotting)")
fi

# Vision module: prefer face_emotion (retinaface+affecnet), fallback to legacy emotion classifier.
if [[ "$VISION_ENABLE" == "true" ]]; then
  HAS_FACE_EMOTION=false
  if [[ -f "$MODELS_DIR/face_emotion/retinaface_mnet_v2_fp16.onnx" && -f "$MODELS_DIR/face_emotion/affecnet7_fp16.onnx" ]]; then
    HAS_FACE_EMOTION=true
  fi
  if [[ -f "$MODELS_DIR/face_emotion/retinaface_mnet_v2_fp16.rknn" && -f "$MODELS_DIR/face_emotion/affecnet7_fp16.rknn" ]]; then
    HAS_FACE_EMOTION=true
  fi
  if [[ "$HAS_FACE_EMOTION" != "true" && ! -f "$MODELS_DIR/emotion/emotion_classifier.onnx" ]]; then
    MODELS_MISSING+=("Vision (need face_emotion/*.onnx or face_emotion/*.rknn or legacy emotion/emotion_classifier.onnx)")
  fi
fi

# TTS local mode: check model files according to selected engine.
if [[ "$TTS_MODE" == "local" ]]; then
  if [[ "$TTS_ENGINE" == "melo-rknn" || ( "$TTS_ENGINE" == "native" && "$TTS_RUNTIME" == "rknnruntime" ) ]]; then
    MELO_MODEL_DIR=$(
      grep -A20 -E "^[[:space:]]*(melo|melo_rknn):" "$TTS_YAML" \
        | grep -E "^[[:space:]]+model_dir:[[:space:]]*" \
        | grep -v "^[[:space:]]*#" \
        | head -1 \
        | awk -F: '{v=$2; gsub(/^[[:space:]]+/, "", v); gsub(/"/, "", v); print v}' || true
    )
    if [[ -z "$MELO_MODEL_DIR" ]]; then
      MELO_MODEL_DIR="melo-tts-rknn"
    fi
    [[ ! -f "$MODELS_DIR/$MELO_MODEL_DIR/checkpoint/rknn/decoder_frame31.rknn" ]] && \
      MODELS_MISSING+=("TTS Melo RKNN ($MELO_MODEL_DIR/checkpoint/rknn/decoder_frame31.rknn)")
    [[ ! -f "$MODELS_DIR/$MELO_MODEL_DIR/model/MeloTTS-ONNX/melo_onnx/text/opencpop-strict.txt" ]] && \
      MODELS_MISSING+=("TTS Melo text resource ($MELO_MODEL_DIR/model/MeloTTS-ONNX/melo_onnx/text/opencpop-strict.txt)")
  elif [[ "$TTS_ENGINE" == "moss-onnx" || "$TTS_ENGINE" == "moss" || "$TTS_ENGINE" == "native" ]]; then
    MOSS_MODEL_DIR=$(
      grep -A20 -E "^[[:space:]]*(moss|moss_onnx):" "$TTS_YAML" \
        | grep -E "^[[:space:]]+model_dir:[[:space:]]*" \
        | grep -v "^[[:space:]]*#" \
        | head -1 \
        | awk -F: '{v=$2; gsub(/^[[:space:]]+/, "", v); gsub(/"/, "", v); print v}' || true
    )
    if [[ -z "$MOSS_MODEL_DIR" ]]; then
      MOSS_MODEL_DIR="moss-tts-nano"
    fi
    [[ ! -f "$MODELS_DIR/$MOSS_MODEL_DIR/MOSS-TTS-Nano-100M-ONNX/moss_tts_global_shared.data" ]] && \
      MODELS_MISSING+=("TTS MOSS ($MOSS_MODEL_DIR/MOSS-TTS-Nano-100M-ONNX/moss_tts_global_shared.data)")
  else
    TTS_MODEL_DIR=$(
      grep -A60 -E "^[[:space:]]*(sherpa|sherpa_onnx):" "$TTS_YAML" \
        | grep -E "^[[:space:]]+model_dir:[[:space:]]*" \
        | grep -v "^[[:space:]]*#" \
        | head -1 \
        | awk -F: '{v=$2; gsub(/^[[:space:]]+/, "", v); gsub(/"/, "", v); print v}' || true
    )
    TTS_MODEL_FILE=$(
      grep -A60 -E "^[[:space:]]*(sherpa|sherpa_onnx):" "$TTS_YAML" \
        | grep -E "^[[:space:]]+model:[[:space:]]*" \
        | grep -v "^[[:space:]]*#" \
        | head -1 \
        | awk -F: '{v=$2; gsub(/^[[:space:]]+/, "", v); gsub(/"/, "", v); print v}' || true
    )
    if [[ -n "$TTS_MODEL_DIR" && -n "$TTS_MODEL_FILE" ]]; then
      [[ ! -f "$MODELS_DIR/$TTS_MODEL_DIR/$TTS_MODEL_FILE" ]] && \
        MODELS_MISSING+=("TTS ($TTS_MODEL_DIR/$TTS_MODEL_FILE)")
    fi
  fi
fi

if [[ ${#MODELS_MISSING[@]} -gt 0 ]]; then
  echo "[ERROR] Missing required models:" >&2
  for m in "${MODELS_MISSING[@]}"; do
    echo "  - $m" >&2
  done
  echo "" >&2
  echo "[HINT] Run: ./scripts/setup_prebuilt.sh models" >&2
  echo "[HINT] Or vision-only: ./scripts/setup_prebuilt.sh vision" >&2
  exit 1
fi

echo "[INFO] Launching buddy_main..."
exec "$BUDDY_MAIN" --ros-args --log-level v4l2_camera:=warn "$@" 2>&1 | sed -E 's/\[([0-9]+\.[0-9]{3})[0-9]{6}\]/[\1]/g'
