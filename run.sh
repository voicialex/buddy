#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARCH="$(uname -m)"
INSTALL_DIR="$ROOT_DIR/output/${ARCH}/install"
SETUP="$INSTALL_DIR/setup.bash"


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

# --- Read configs to determine local vs remote modes ---
PARAMS_DIR="$INSTALL_DIR/buddy_app/share/buddy_app/params"
AUDIO_YAML="$PARAMS_DIR/audio.yaml"
BRAIN_YAML="$PARAMS_DIR/brain.yaml"
CLOUD_YAML="$PARAMS_DIR/cloud.yaml"

TTS_MODE=$(grep -A2 "^    tts:" "$AUDIO_YAML" | grep "mode:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")
TTS_ENGINE=$(grep -A3 "^    tts:" "$AUDIO_YAML" | grep "engine:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")
TTS_URL=$(grep -A2 "chattts:" "$AUDIO_YAML" | grep "url:" | head -1 | awk '{print $2}' | tr -d '"' || echo "—")
ASR_MODE=$(grep -A2 "^    asr:" "$AUDIO_YAML" | grep "mode:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")
ASR_ENGINE=$(grep -A3 "^    asr:" "$AUDIO_YAML" | grep "engine:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")
ASR_URL=$(grep -A2 "funasr:" "$AUDIO_YAML" | grep "url:" | head -1 | awk '{print $2}' | tr -d '"' || echo "—")
INFERENCE_MODE=$(grep -A1 "inference:" "$BRAIN_YAML" | grep "mode:" | awk '{print $2}' | tr -d '"' || echo "unknown")
CLOUD_MODEL=$(grep "model:" "$CLOUD_YAML" | awk '{print $2}' | tr -d '"' || echo "—")
CLOUD_ENDPOINT=$(grep "endpoint:" "$CLOUD_YAML" | awk '{print $2}' | tr -d '"' || echo "—")

# --- Display unified status ---
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Buddy Pipeline Status"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "  [Module Config]"
echo "    ASR .......... $ASR_MODE (engine=$ASR_ENGINE)"
if [[ "$ASR_MODE" == "server" ]]; then
echo "                   endpoint: $ASR_URL"
fi
echo "    TTS .......... $TTS_MODE (engine=$TTS_ENGINE)"
if [[ "$TTS_MODE" == "server" ]]; then
echo "                   endpoint: $TTS_URL"
fi
echo "    Inference .... $INFERENCE_MODE"
if [[ "$INFERENCE_MODE" == *"cloud"* ]] || [[ "$INFERENCE_MODE" == "hybrid" ]]; then
echo "                   model:    $CLOUD_MODEL"
echo "                   endpoint: $CLOUD_ENDPOINT"
fi
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# --- Check critical models ---
MODELS_DIR="$ROOT_DIR/models"
MODELS_MISSING=()

# ASR model (always needed for wake word + speech recognition)
[[ ! -f "$MODELS_DIR/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/tokens.txt" ]] && \
  MODELS_MISSING+=("ASR (sherpa-onnx-streaming-zipformer)")
[[ ! -f "$MODELS_DIR/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile/tokens.txt" ]] && \
  MODELS_MISSING+=("KWS (keyword spotting)")
[[ ! -f "$MODELS_DIR/vits-icefall-zh-aishell3/model.onnx" ]] && \
  MODELS_MISSING+=("TTS (vits-icefall-zh-aishell3)")

if [[ ${#MODELS_MISSING[@]} -gt 0 ]]; then
  echo "[ERROR] Missing required models:" >&2
  for m in "${MODELS_MISSING[@]}"; do
    echo "  - $m" >&2
  done
  echo "" >&2
  echo "[HINT] Run: ./scripts/setup_prebuilt.sh models" >&2
  exit 1
fi

echo "[INFO] Launching buddy_main..."
exec "$BUDDY_MAIN" "$@" 2>&1 | sed -E 's/\[([0-9]+\.[0-9]{3})[0-9]{6}\]/[\1]/g'
