#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export LD_LIBRARY_PATH="$DIR/lib:$DIR/lib/sherpa:$DIR/lib/funasr:${LD_LIBRARY_PATH:-}"

if [[ -f "$DIR/etc/buddy.env" ]]; then
    set -a; source "$DIR/etc/buddy.env"; set +a
fi

# --- Read configs to determine modes ---
AUDIO_YAML="$DIR/params/audio.yaml"
BRAIN_YAML="$DIR/params/brain.yaml"
CLOUD_YAML="$DIR/params/cloud.yaml"

TTS_MODE="unknown"; TTS_ENGINE="unknown"; TTS_URL="—"
ASR_MODE="unknown"; ASR_ENGINE="unknown"; ASR_URL="—"
INFERENCE_MODE="unknown"; CLOUD_MODEL="—"; CLOUD_ENDPOINT="—"

if [[ -f "$AUDIO_YAML" ]]; then
  TTS_MODE=$(grep -A2 "^    tts:" "$AUDIO_YAML" | grep "mode:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")
  TTS_ENGINE=$(grep -A3 "^    tts:" "$AUDIO_YAML" | grep "engine:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")
  TTS_URL=$(grep -A2 "chattts:" "$AUDIO_YAML" | grep "url:" | head -1 | awk '{print $2}' | tr -d '"' || echo "—")
  ASR_MODE=$(grep -A2 "^    asr:" "$AUDIO_YAML" | grep "mode:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")
  ASR_ENGINE=$(grep -A3 "^    asr:" "$AUDIO_YAML" | grep "engine:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")
  ASR_URL=$(grep -A2 "funasr:" "$AUDIO_YAML" | grep "url:" | head -1 | awk '{print $2}' | tr -d '"' || echo "—")
fi
if [[ -f "$BRAIN_YAML" ]]; then
  INFERENCE_MODE=$(grep -A1 "inference:" "$BRAIN_YAML" | grep "mode:" | awk '{print $2}' | tr -d '"' || echo "unknown")
fi
if [[ -f "$CLOUD_YAML" ]]; then
  CLOUD_MODEL=$(grep "model:" "$CLOUD_YAML" | awk '{print $2}' | tr -d '"' || echo "—")
  CLOUD_ENDPOINT=$(grep "endpoint:" "$CLOUD_YAML" | awk '{print $2}' | tr -d '"' || echo "—")
fi

# --- Colors ---
C_GREEN='\033[0;32m'; C_RED='\033[0;31m'; C_YELLOW='\033[0;33m'; C_RESET='\033[0m'
TAG_RUN="${C_GREEN}[RUN]${C_RESET}"
TAG_STOP="${C_RED}[STOP]${C_RESET}"
TAG_SKIP="${C_YELLOW}[SKIP]${C_RESET}"

# --- Check services ---
TTS_TAG=""; OLLAMA_TAG=""; ASR_TAG=""
CHAT_TTS_URL="${TTS_URL%/tts}/docs"
OLLAMA_URL="http://127.0.0.1:11434/api/tags"
ASR_CHECK_URL="${ASR_URL:-}"

if [[ "$ASR_MODE" == "server" ]] && [[ -n "$ASR_URL" ]]; then
  # WebSocket endpoint — try TCP connect
  ASR_HOST=$(echo "$ASR_URL" | sed -E 's|wss?://||;s|/.*||;s|:.*||')
  ASR_PORT=$(echo "$ASR_URL" | sed -E 's|wss?://[^:]+:||;s|/.*||')
  if timeout 2 bash -c "echo >/dev/tcp/$ASR_HOST/${ASR_PORT:-10095}" 2>/dev/null; then
    ASR_TAG="$TAG_RUN"
  else
    ASR_TAG="$TAG_STOP"
  fi
fi

if [[ "$TTS_MODE" == "server" ]]; then
  s=$(curl -sS -o /dev/null -w "%{http_code}" --max-time 2 "$CHAT_TTS_URL" 2>/dev/null || true)
  [[ "$s" =~ ^[23] ]] && TTS_TAG="$TAG_RUN" || TTS_TAG="$TAG_STOP"
fi

if [[ "$INFERENCE_MODE" == *"local"* ]] || [[ "$INFERENCE_MODE" == "hybrid" ]]; then
  s=$(curl -sS -o /dev/null -w "%{http_code}" --max-time 2 "$OLLAMA_URL" 2>/dev/null || true)
  [[ "$s" =~ ^[23] ]] && OLLAMA_TAG="$TAG_RUN" || OLLAMA_TAG="$TAG_STOP"
elif command -v ollama &>/dev/null; then
  # ollama installed but not needed for current mode — show as SKIP
  OLLAMA_TAG="$TAG_SKIP"
fi

# --- Display status ---
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Buddy Pipeline Status"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "  [Module Config]"
echo "    ASR .......... $ASR_MODE (engine=$ASR_ENGINE)"
[[ "$ASR_MODE" == "server" ]] && echo "                   endpoint: $ASR_URL"
echo "    TTS .......... $TTS_MODE (engine=$TTS_ENGINE)"
[[ "$TTS_MODE" == "server" ]] && echo "                   endpoint: $TTS_URL"
echo "    Inference .... $INFERENCE_MODE"
if [[ "$INFERENCE_MODE" == *"cloud"* ]] || [[ "$INFERENCE_MODE" == "hybrid" ]]; then
  echo "                   model:    $CLOUD_MODEL"
  echo "                   endpoint: $CLOUD_ENDPOINT"
fi
echo ""
echo "  [Service Health]"

if [[ "$ASR_MODE" == "server" ]]; then
  echo -e "    FunASR ....... $ASR_TAG  $ASR_URL"
  [[ "$ASR_TAG" == "$TAG_STOP" ]] && echo -e "                   ${C_YELLOW}> $DIR/scripts/start_asr_server.sh${C_RESET}"
else
  echo -e "    FunASR ....... $TAG_SKIP not needed (ASR mode=$ASR_MODE)"
fi

if [[ "$TTS_MODE" == "server" ]]; then
  echo -e "    ChatTTS ...... $TTS_TAG  $TTS_URL"
  [[ "$TTS_TAG" == "$TAG_STOP" ]] && echo -e "                   ${C_YELLOW}> $DIR/scripts/start_tts_server.sh${C_RESET}"
else
  echo -e "    ChatTTS ...... $TAG_SKIP not needed (TTS mode=$TTS_MODE)"
fi

if [[ "$INFERENCE_MODE" == *"local"* ]] || [[ "$INFERENCE_MODE" == "hybrid" ]]; then
  echo -e "    Ollama ....... $OLLAMA_TAG  $OLLAMA_URL"
  [[ "$OLLAMA_TAG" == "$TAG_STOP" ]] && echo -e "                   ${C_YELLOW}> ollama serve${C_RESET}"
elif command -v ollama &>/dev/null; then
  echo -e "    Ollama ....... $TAG_SKIP installed but not needed (inference=$INFERENCE_MODE)"
else
  echo -e "    Ollama ....... $TAG_SKIP not installed"
  echo -e "                   ${C_YELLOW}> curl -fsSL https://ollama.com/install.sh | sh${C_RESET}"
fi

if [[ "$INFERENCE_MODE" == *"cloud"* ]] || [[ "$INFERENCE_MODE" == "hybrid" ]]; then
  echo -e "    Cloud LLM .... $TAG_RUN  $CLOUD_MODEL"
else
  echo -e "    Cloud LLM .... $TAG_SKIP not needed (inference=$INFERENCE_MODE)"
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "[INFO] Launching buddy_main..."
cd "$DIR"
exec "$DIR/bin/buddy_main" --base-dir "$DIR"
