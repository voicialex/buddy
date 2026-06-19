#!/usr/bin/env bash
# 查看 buddy 管线与服务运行状态
#
# Usage:
#   ./scripts/status.sh              # Full: pipeline + services + app
#   ./scripts/status.sh --pipeline   # Pipeline + services only
#   ./scripts/status.sh --module     # Module config only
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/common.sh"

# Detect actual runtime hardware from prebuilt libs
PREBUILT_CURRENT="$BASE_DIR/prebuilt/current"
RUNTIME_HW="cpu"
if [[ -d "$PREBUILT_CURRENT/rknn" ]]; then
    RUNTIME_HW="npu"
elif [[ -d "$PREBUILT_CURRENT/onnxruntime-gpu" ]]; then
    RUNTIME_HW="gpu"
fi

COL_GREEN="\033[32m"
COL_RED="\033[31m"
COL_YELLOW="\033[33m"
COL_RESET="\033[0m"

PIPELINE_ONLY=false
MODULE_ONLY=false
if [[ "${1:-}" == "--pipeline" ]]; then
    PIPELINE_ONLY=true
elif [[ "${1:-}" == "--module" ]]; then
    MODULE_ONLY=true
fi

pick_file() {
    local f
    for f in "$@"; do
        if [[ -f "$f" ]]; then
            echo "$f"
            return 0
        fi
    done
    echo ""
}

AUDIO_ASR_YAML="$(pick_file "$BASE_DIR/params/audio.asr.yaml" "$BASE_DIR/src/buddy_app/params/audio.asr.yaml")"
AUDIO_TTS_YAML="$(pick_file "$BASE_DIR/params/audio.tts.yaml" "$BASE_DIR/src/buddy_app/params/audio.tts.yaml")"
VISION_YAML="$(pick_file "$BASE_DIR/params/vision.yaml" "$BASE_DIR/src/buddy_app/params/vision.yaml")"
MODULES_YAML="$(pick_file "$BASE_DIR/params/modules.yaml" "$BASE_DIR/src/buddy_app/params/modules.yaml")"
LLM_BRIDGE_YAML="$(pick_file "$BASE_DIR/params/llm_bridge.yaml" "$BASE_DIR/src/buddy_app/params/llm_bridge.yaml")"
LLM_CONFIG="$(pick_file "$BASE_DIR/services/llm/config.yaml")"
BUDDY_ENV="$(pick_file "$BASE_DIR/etc/buddy.env")"

if [[ -f "$BUDDY_ENV" ]]; then
    set -a
    # shellcheck disable=SC1090
    source "$BUDDY_ENV"
    set +a
fi

asr_mode="unknown"
asr_engine_cfg="unknown"
asr_runtime_cfg="unknown"
tts_mode="unknown"
tts_engine_cfg="unknown"
tts_runtime_cfg="unknown"
inference_mode="unknown"
inference_backend="unknown"
vision_enabled="true"
vision_engine_cfg="auto"
vision_runtime_cfg="auto"

if [[ -f "$AUDIO_ASR_YAML" ]]; then
    asr_mode="$(grep -A2 "^    asr:" "$AUDIO_ASR_YAML" | grep "mode:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")"
    asr_engine_cfg="$(grep -A3 "^    asr:" "$AUDIO_ASR_YAML" | grep "engine:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")"
    asr_runtime_cfg="$(grep -A4 "^    asr:" "$AUDIO_ASR_YAML" | grep "runtime:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")"
fi

if [[ -f "$AUDIO_TTS_YAML" ]]; then
    tts_mode="$(grep -A2 "^    tts:" "$AUDIO_TTS_YAML" | grep "mode:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")"
    tts_engine_cfg="$(grep -A3 "^    tts:" "$AUDIO_TTS_YAML" | grep "engine:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")"
    tts_runtime_cfg="$(grep -A4 "^    tts:" "$AUDIO_TTS_YAML" | grep "runtime:" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")"
fi

if [[ -f "$LLM_BRIDGE_YAML" ]]; then
    inference_mode="$(grep -E "^[[:space:]]*mode:" "$LLM_BRIDGE_YAML" | head -1 | awk '{print $2}' | tr -d '"' || echo "unknown")"
fi

if [[ -f "$MODULES_YAML" ]]; then
    vision_enabled="$(grep -A10 "^modules:" "$MODULES_YAML" | grep "vision:" | head -1 | awk '{print $2}' | tr -d '"' || echo "true")"
fi

if [[ -f "$VISION_YAML" ]]; then
    vision_engine_cfg="$(grep -A8 "^vision:" "$VISION_YAML" | grep "engine:" | head -1 | awk '{print $2}' | tr -d '"' || echo "auto")"
    vision_runtime_cfg="$(grep -A8 "^vision:" "$VISION_YAML" | grep "runtime:" | head -1 | awk '{print $2}' | tr -d '"' || echo "auto")"
fi

resolve_local_backend_mode() {
    local requested
    requested="${BUDDY_LLM_BACKEND:-}"
    if [[ -n "$requested" ]]; then
        echo "$requested"
        return 0
    fi

    requested="ollama"
    if [[ -f "$LLM_CONFIG" ]]; then
        requested="$(grep -E '^[[:space:]]*active_backend:' "$LLM_CONFIG" | head -1 | awk -F: '{print $2}' | tr -d ' "' || true)"
        [[ -z "$requested" ]] && requested="ollama"
    fi

    if [[ "$requested" != "auto" ]]; then
        echo "$requested"
        return 0
    fi

    local arch="${BUDDY_TARGET_ARCH:-}"
    local device="${BUDDY_TARGET_DEVICE:-}"
    arch="$(echo "$arch" | tr '[:upper:]' '[:lower:]')"
    device="$(echo "$device" | tr '[:upper:]' '[:lower:]')"
    if [[ "$arch" =~ ^(arm64|aarch64)$ ]] && [[ "$device" == "npu" ]]; then
        echo "rk_llm"
    else
        echo "ollama"
    fi
}

has_rknn_runtime() {
    if [[ -f "$BASE_DIR/lib/librknnrt.so" ]]; then
        return 0
    fi
    if command -v ldconfig >/dev/null 2>&1 && ldconfig -p 2>/dev/null | grep -q "librknnrt.so"; then
        return 0
    fi
    return 1
}

resolve_asr_runtime() {
    local runtime="$asr_runtime_cfg"
    if [[ "$asr_mode" == "server" ]]; then
        echo "server"
        return 0
    fi
    if [[ "$runtime" == "auto" ]]; then
        if has_rknn_runtime; then
            echo "rknnruntime"
        else
            echo "onnxruntime"
        fi
    else
        echo "$runtime"
    fi
}

resolve_asr_engine() {
    local runtime="$1"
    local engine="$asr_engine_cfg"
    if [[ "$asr_mode" == "server" ]]; then
        echo "server"
        return 0
    fi
    if [[ "$engine" == "auto" ]]; then
        if [[ "$runtime" == "rknnruntime" ]]; then
            echo "native"
        else
            echo "sherpa-onnx"
        fi
    else
        echo "$engine"
    fi
}

resolve_tts_runtime() {
    local runtime="$tts_runtime_cfg"
    if [[ "$tts_mode" == "server" ]]; then
        echo "server"
        return 0
    fi
    if [[ "$runtime" == "auto" ]]; then
        echo "onnxruntime"
    else
        echo "$runtime"
    fi
}

resolve_tts_engine() {
    local runtime="$1"
    local engine="$tts_engine_cfg"
    if [[ "$tts_mode" == "server" ]]; then
        echo "server"
        return 0
    fi
    if [[ "$engine" == "auto" ]]; then
        if [[ "$runtime" == "rknnruntime" ]]; then
            echo "native"
        else
            echo "sherpa-onnx"
        fi
    else
        echo "$engine"
    fi
}

resolve_rkllm_url() {
    local base_url="${BUDDY_RKLLM_URL_BASE:-http://127.0.0.1:8080}"
    local endpoint="${BUDDY_RKLLM_ENDPOINT:-/rkllm_chat}"

    if [[ -f "$LLM_CONFIG" ]]; then
        local parsed
        parsed="$(
            awk '
                /^    rk_llm:[[:space:]]*$/ {in_rk=1; next}
                in_rk && /^    [A-Za-z0-9_]+:[[:space:]]*$/ {in_rk=0}
                in_rk && /^      base_url:[[:space:]]*/ {
                    value=$0; sub(/^[^:]*:[[:space:]]*/, "", value); gsub(/"/, "", value); base=value
                }
                in_rk && /^      endpoint:[[:space:]]*/ {
                    value=$0; sub(/^[^:]*:[[:space:]]*/, "", value); gsub(/"/, "", value); ep=value
                }
                END {if (base != "") print "BASE=" base; if (ep != "") print "ENDPOINT=" ep}
            ' "$LLM_CONFIG"
        )"
        if [[ -n "$parsed" ]]; then
            # shellcheck disable=SC1090
            source /dev/stdin <<<"$parsed"
            [[ -n "${BASE:-}" ]] && base_url="$BASE"
            [[ -n "${ENDPOINT:-}" ]] && endpoint="$ENDPOINT"
        fi
    fi

    [[ "$endpoint" == /* ]] || endpoint="/$endpoint"
    echo "${base_url}${endpoint}"
}

check_rkllm_ready() {
    local url="$1"
    local code
    code="$(
        curl -sS -o /dev/null -w "%{http_code}" --max-time 8 \
            -H "Content-Type: application/json" \
            -d '{"model":"buddy","messages":[{"role":"user","content":"ping"}],"stream":false}' \
            "$url" 2>/dev/null || true
    )"
    [[ "$code" == "200" || "$code" == "503" ]]
}

asr_runtime="$(resolve_asr_runtime)"
asr_engine="$(resolve_asr_engine "$asr_runtime")"
tts_runtime="$(resolve_tts_runtime)"
tts_engine="$(resolve_tts_engine "$tts_runtime")"
inference_backend="$(resolve_local_backend_mode)"

asr_hw="unknown"
if [[ "$asr_mode" == "local" ]]; then
    if [[ "$asr_runtime" == "rknnruntime" ]]; then
        asr_hw="NPU"
    elif [[ "$RUNTIME_HW" == "gpu" ]]; then
        asr_hw="GPU"
    else
        asr_hw="CPU"
    fi
elif [[ "$asr_mode" == "server" ]]; then
    asr_hw="service"
fi

tts_hw="unknown"
if [[ "$tts_mode" == "local" ]]; then
    if [[ "$tts_runtime" == "rknnruntime" ]]; then
        tts_hw="NPU"
    elif [[ "$RUNTIME_HW" == "gpu" ]]; then
        tts_hw="GPU"
    else
        tts_hw="CPU"
    fi
elif [[ "$tts_mode" == "server" ]]; then
    tts_hw="service"
fi

inference_hw="unknown"
if [[ "$inference_mode" == "local_route" || "$inference_mode" == "local_only" || "$inference_mode" == "hybrid" ]]; then
    if [[ "$inference_backend" == "rk_llm" ]]; then
        inference_hw="NPU"
    elif [[ "$inference_backend" == "ollama" || "$inference_backend" == "vllm" ]]; then
        if [[ "${BUILD_DEVICE:-${BUDDY_TARGET_DEVICE:-cpu}}" == "gpu" ]]; then
            inference_hw="GPU"
        else
            inference_hw="CPU"
        fi
    fi
fi

resolve_vision_runtime() {
    local runtime="$vision_runtime_cfg"
    if [[ "$vision_enabled" != "true" ]]; then
        echo "disabled"
        return 0
    fi
    if [[ "$runtime" == "auto" ]]; then
        if has_rknn_runtime; then
            echo "rknnruntime"
        else
            echo "onnxruntime"
        fi
    else
        echo "$runtime"
    fi
}

vision_runtime="$(resolve_vision_runtime)"
vision_hw="off"
if [[ "$vision_enabled" == "true" ]]; then
    if [[ "$vision_runtime" == "rknnruntime" ]]; then
        vision_hw="NPU"
    elif [[ "$RUNTIME_HW" == "gpu" ]]; then
        vision_hw="GPU"
    else
        vision_hw="CPU"
    fi
fi

need_rkllm=false
need_ollama=false
need_funasr=false
need_chattts=false
need_llm_api=false

[[ "$asr_mode" == "server" ]] && need_funasr=true
[[ "$tts_mode" == "server" ]] && need_chattts=true
if [[ "$inference_mode" != "unknown" ]]; then
    need_llm_api=true
fi
if [[ "$inference_mode" == "local_route" || "$inference_mode" == "local_only" || "$inference_mode" == "hybrid" ]]; then
    if [[ "$inference_backend" == "rk_llm" ]]; then
        need_rkllm=true
    elif [[ "$inference_backend" == "ollama" ]]; then
        need_ollama=true
    fi
fi

SERVICES=(
    "RKLLM|$(resolve_rkllm_url)|8080"
    "Ollama|http://localhost:11434|11434"
    "LLM API|http://127.0.0.1:8002/health|8002"
    "ChatTTS|http://127.0.0.1:9880/docs|9880"
    "FunASR|ws://127.0.0.1:10095|10095"
)

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Buddy Pipeline Status"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "  [Module Config]"
echo "    ASR .......... ${asr_mode} (engine=${asr_engine}, runtime=${asr_runtime}, hw=${asr_hw})"
echo "    TTS .......... ${tts_mode} (engine=${tts_engine}, runtime=${tts_runtime}, hw=${tts_hw})"
if [[ "$vision_enabled" == "true" ]]; then
echo "    Vision ....... enabled (engine=${vision_engine_cfg}, runtime=${vision_runtime}, hw=${vision_hw})"
else
echo "    Vision ....... disabled"
fi
if [[ "$inference_mode" == "local_route" || "$inference_mode" == "local_only" || "$inference_mode" == "hybrid" ]]; then
echo "    Inference .... ${inference_mode} (backend=${inference_backend}, hw=${inference_hw})"
else
    echo "    Inference .... ${inference_mode}"
fi
echo ""

if [[ "$MODULE_ONLY" == true ]]; then
    exit 0
fi

echo "  [Service Health]"
echo ""
printf "  %-12s %-10s %-6s %s\n" "SERVICE" "STATUS" "PORT" "DETAIL"
printf "  %-12s %-10s %-6s %s\n" "-------" "------" "----" "------"

for entry in "${SERVICES[@]}"; do
    IFS='|' read -r name url port <<< "$entry"

    status=""
    detail=""

    if [[ "$name" == "RKLLM" && "$need_rkllm" != true ]]; then
        status="${COL_YELLOW}skip${COL_RESET}"
        detail="not needed (backend=${inference_backend})"
        printf "  %-12s %-22b %-6s %s\n" "$name" "$status" "$port" "$detail"
        continue
    fi
    if [[ "$name" == "Ollama" && "$need_ollama" != true ]]; then
        status="${COL_YELLOW}skip${COL_RESET}"
        detail="not needed (backend=${inference_backend})"
        printf "  %-12s %-22b %-6s %s\n" "$name" "$status" "$port" "$detail"
        continue
    fi
    if [[ "$name" == "LLM API" && "$need_llm_api" != true ]]; then
        status="${COL_YELLOW}skip${COL_RESET}"
        detail="not needed (inference=${inference_mode})"
        printf "  %-12s %-22b %-6s %s\n" "$name" "$status" "$port" "$detail"
        continue
    fi
    if [[ "$name" == "ChatTTS" && "$need_chattts" != true ]]; then
        status="${COL_YELLOW}skip${COL_RESET}"
        detail="not needed (TTS mode=${tts_mode})"
        printf "  %-12s %-22b %-6s %s\n" "$name" "$status" "$port" "$detail"
        continue
    fi
    if [[ "$name" == "FunASR" && "$need_funasr" != true ]]; then
        status="${COL_YELLOW}skip${COL_RESET}"
        detail="not needed (ASR mode=${asr_mode})"
        printf "  %-12s %-22b %-6s %s\n" "$name" "$status" "$port" "$detail"
        continue
    fi

    if [[ "$name" == "RKLLM" ]]; then
        if check_port "$port" && check_rkllm_ready "$url"; then
            status="${COL_GREEN}running${COL_RESET}"
            detail="$url ✓"
        elif check_port "$port"; then
            status="${COL_RED}stopped${COL_RESET}"
            detail="$url (port up, api not ready)"
        else
            status="${COL_RED}stopped${COL_RESET}"
            detail="$url"
        fi
    elif check_port "$port"; then
        status="${COL_GREEN}running${COL_RESET}"
        if [[ "$url" == http* ]]; then
            if check_http "$url"; then
                detail="$url ✓"
            else
                detail="$url (not responding)"
            fi
        else
            detail="$url ✓"
        fi
    else
        status="${COL_RED}stopped${COL_RESET}"
    fi

    printf "  %-12s %-22b %-6s %s\n" "$name" "$status" "$port" "$detail"
done

if [[ "$need_ollama" == true ]] && check_http "http://localhost:11434"; then
    echo ""
    echo "  Ollama models loaded:"
    ollama ps 2>/dev/null | sed 's/^/    /' || echo "    (none)"
fi

echo ""

if [[ "$PIPELINE_ONLY" == true ]]; then
    exit 0
fi

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Buddy App (C++ ROS 2)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

BUDDY_PIDS=$(pgrep -f buddy_main 2>/dev/null || true)
if [ -n "$BUDDY_PIDS" ]; then
    printf "  %-12s %b\n" "Process" "${COL_GREEN}running${COL_RESET}  (PID: $(echo "$BUDDY_PIDS" | tr '\n' ' '))"
    if command -v ps >/dev/null 2>&1; then
        for pid in $BUDDY_PIDS; do
            elapsed=$(ps -o etime= -p "$pid" 2>/dev/null | tr -d ' ')
            if [ -n "$elapsed" ]; then
                printf "    PID %-6s uptime: %s\n" "$pid" "$elapsed"
            fi
        done
    fi
else
    printf "  %-12s %b\n" "Process" "${COL_RED}stopped${COL_RESET}"
fi

if command -v systemctl >/dev/null 2>&1; then
    svc_status=$(systemctl is-active buddy 2>/dev/null || true)
    case "$svc_status" in
        active)   svc_color="${COL_GREEN}active${COL_RESET}" ;;
        inactive) svc_color="${COL_RED}inactive${COL_RESET}" ;;
        "")       svc_color="${COL_RED}not installed${COL_RESET}" ;;
        *)        svc_color="${COL_RED}${svc_status}${COL_RESET}" ;;
    esac
    printf "  %-12s %b\n" "systemd" "$svc_color"
fi

if command -v ros2 >/dev/null 2>&1; then
    nodes=$(ros2 node list 2>/dev/null || true)
    if [ -n "$nodes" ]; then
        echo ""
        echo "  ROS 2 nodes:"
        echo "$nodes" | sed 's/^/    /'
    fi
fi

echo ""
