#!/usr/bin/env bash
# Switch ASR / TTS / Vision runtime between NPU (RKNN) and CPU (ONNX).
#
# Usage:
#   ./scripts/switch_runtime.sh npu     # all three → NPU
#   ./scripts/switch_runtime.sh cpu     # all three → CPU
#   ./scripts/switch_runtime.sh status  # show current config
#   ./scripts/switch_runtime.sh npu asr # only ASR
#   ./scripts/switch_runtime.sh npu tts vision
#
# No reboot needed — just restart buddy_main (./run.sh) afterwards.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PARAMS_DIR="$PROJECT_DIR/params"

# Fallbacks for dev layout (no install tree yet)
if [[ ! -d "$PARAMS_DIR" ]]; then
    PARAMS_DIR="$PROJECT_DIR/src/buddy_app/params"
fi

ASR_YAML="$PARAMS_DIR/audio.asr.yaml"
TTS_YAML="$PARAMS_DIR/audio.tts.yaml"
VISION_YAML="$PARAMS_DIR/vision.yaml"

# Color codes (only in TTY)
if [[ -t 1 && "${TERM:-}" != "dumb" ]]; then
    C_GREEN=$'\033[32m'; C_RED=$'\033[31m'; C_CYAN=$'\033[36m'
    C_YELLOW=$'\033[33m'; C_BOLD=$'\033[1m'; C_RESET=$'\033[0m'
else
    C_GREEN=""; C_RED=""; C_CYAN=""; C_YELLOW=""; C_BOLD=""; C_RESET=""
fi

usage() {
    cat <<EOF
Usage: $0 <target> [module ...]

Targets:
  npu     Switch modules to NPU (RKNN runtime)
  cpu     Switch modules to CPU (ONNX runtime)
  status  Show current runtime config (no changes)

Modules (default: all):
  asr     ASR engine  (zipformer-rknn / sherpa-onnx)
  tts     TTS engine  (melo-rknn / sherpa-onnx)
  vision  Vision      (retinaface + rknnruntime / onnxruntime)

Examples:
  $0 npu              # all three → NPU
  $0 cpu              # all three → CPU
  $0 status           # show current
  $0 npu asr tts      # only ASR + TTS → NPU
EOF
    exit 0
}

# Per-module config: which file, which fields, npu values, cpu values.
# Format: <file>:<engine_field>:<npu_engine>:<cpu_engine>:<runtime_field>
declare -A MODULE_FILES=(
    [asr]="$ASR_YAML"
    [tts]="$TTS_YAML"
    [vision]="$VISION_YAML"
)

declare -A NPU_ENGINE=(
    [asr]="zipformer-rknn"
    [tts]="melo-rknn"
    [vision]="retinaface"
)

declare -A CPU_ENGINE=(
    [asr]="sherpa-onnx"
    [tts]="sherpa-onnx"
    [vision]="retinaface"
)

# Set engine + runtime for a module.
# Args: <module> <target(npu|cpu)>
set_module() {
    local module="$1" target="$2"
    local file="${MODULE_FILES[$module]:-}"
    [[ -z "$file" || ! -f "$file" ]] && { echo "${C_RED}[!] $module: file not found ($file)${C_RESET}" >&2; return 1; }

    local engine runtime
    if [[ "$target" == "npu" ]]; then
        engine="${NPU_ENGINE[$module]}"
        runtime="rknnruntime"
    else
        engine="${CPU_ENGINE[$module]}"
        runtime="onnxruntime"
    fi

    # Vision has no engine swap (retinaface stays), only runtime changes.
    # We still rewrite engine for consistency (it's a no-op sed).
    sed -i -E \
        -e "s|^([[:space:]]*)engine:[[:space:]]*\"[^\"]*\"|\1engine: \"$engine\"|" \
        -e "s|^([[:space:]]*)runtime:[[:space:]]*\"[^\"]*\"|\1runtime: \"$runtime\"|" \
        "$file"
    echo "${C_GREEN}[OK]${C_RESET} $module → $target (engine=$engine, runtime=$runtime)"
}

# Read current runtime field from a module's yaml.
get_module_runtime() {
    local module="$1"
    local file="${MODULE_FILES[$module]:-}"
    [[ -z "$file" || ! -f "$file" ]] && { echo "missing"; return; }
    local val
    val="$(grep -E '^\s*runtime:' "$file" | head -1 | awk '{print $2}' | tr -d '"' || true)"
    echo "${val:-unknown}"
}

get_module_engine() {
    local module="$1"
    local file="${MODULE_FILES[$module]:-}"
    [[ -z "$file" || ! -f "$file" ]] && { echo "missing"; return; }
    local val
    val="$(grep -E '^\s*engine:' "$file" | head -1 | awk '{print $2}' | tr -d '"' || true)"
    echo "${val:-unknown}"
}

show_status() {
    echo ""
    echo "${C_BOLD}Current runtime config:${C_RESET}"
    echo ""
    printf "  %-8s %-20s %-15s %s\n" "Module" "Engine" "Runtime" "HW"
    printf "  %-8s %-20s %-15s %s\n" "------" "------" "-------" "--"
    local m engine runtime hw
    for m in asr tts vision; do
        engine="$(get_module_engine "$m")"
        runtime="$(get_module_runtime "$m")"
        case "$runtime" in
            rknnruntime) hw="${C_CYAN}NPU${C_RESET}" ;;
            onnxruntime) hw="${C_YELLOW}CPU${C_RESET}" ;;
            *) hw="${C_RED}?${C_RESET}" ;;
        esac
        printf "  %-8s %-20s %-15s %b\n" "$m" "$engine" "$runtime" "$hw"
    done
    echo ""
    echo "Switch:  $0 npu | $0 cpu"
}

# ── Parse args ──────────────────────────────────────────────────────
if [[ $# -lt 1 ]]; then
    usage
fi

target="$1"; shift
case "$target" in
    -h|--help) usage ;;
    status) show_status; exit 0 ;;
    npu|cpu) ;;
    *) echo "${C_RED}[!] Unknown target: $target${C_RESET}" >&2; usage ;;
esac

# Default: all three modules
modules=("$@")
[[ ${#modules[@]} -eq 0 ]] && modules=(asr tts vision)

# Validate module names
for m in "${modules[@]}"; do
    if [[ -z "${MODULE_FILES[$m]:-}" ]]; then
        echo "${C_RED}[!] Unknown module: $m${C_RESET}" >&2
        echo "Valid: asr tts vision" >&2
        exit 1
    fi
done

echo ""
echo "${C_BOLD}Switching to $target:${C_RESET} ${modules[*]}"
echo ""
for m in "${modules[@]}"; do
    set_module "$m" "$target"
done

show_status

cat <<EOF

${C_YELLOW}Note:${C_RESET} restart buddy_main to apply changes:
  cd "$PROJECT_DIR" && ./run.sh
  # or, under systemd:  sudo systemctl restart buddy
EOF
