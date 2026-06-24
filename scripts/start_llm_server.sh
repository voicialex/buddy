#!/usr/bin/env bash
# Unified LLM Service startup script
#
# Manages both Ollama backend and Python FastAPI service in one place.
#
# Usage:
#   ./scripts/start_llm_server.sh           # Start everything (Ollama + Python)
#   ./scripts/start_llm_server.sh restart   # Restart Python service only (reload code/config)
#   ./scripts/start_llm_server.sh -f        # Foreground mode (Python service)
#   ./scripts/start_llm_server.sh stop      # Stop both services
#   ./scripts/start_llm_server.sh install   # Install only (Ollama + venv + model), don't start
#
# Endpoints:
#   Ollama:  http://localhost:11434
#   LLM API: http://localhost:8002
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Load buddy.env if present (sets BUDDY_LLM_BACKEND, BUDDY_TARGET_DEVICE, etc.)
if [[ -f "$PROJECT_DIR/etc/buddy.env" ]]; then
    set -a; source "$PROJECT_DIR/etc/buddy.env"; set +a
fi

# LD_LIBRARY_PATH is set inline when launching Python services (start_llm_service, start_rkllm)
# to avoid polluting system tools like curl with bundled libcurl.
BUDDY_LD_LIBRARY_PATH="$PROJECT_DIR/lib:$PROJECT_DIR/lib/sherpa:$PROJECT_DIR/lib/funasr:${LD_LIBRARY_PATH:-}"

# ── Ollama config ──
OLLAMA_LOG="/tmp/buddy-ollama.log"
OLLAMA_MODEL="${BUDDY_OLLAMA_MODEL:-}"
OLLAMA_URL="http://localhost:11434"
OLLAMA_BACKEND="${OLLAMA_LLM_LIBRARY:-auto}"  # auto | cpu | cuda_v12

# ── Python LLM service config ──
SERVICE_DIR="$PROJECT_DIR/services/llm"
VENV_DIR="$SERVICE_DIR/.venv"
LLM_CONFIG="$SERVICE_DIR/config.yaml"
LLM_LOG="/tmp/buddy-llm-server.log"
LLM_PORT=8002
PROC_PATTERN="python.*services/llm/server.py"
OLLAMA_PROC_PATTERN="ollama serve"
RKLLM_LOG="/tmp/buddy-rkllm-server.log"
RKLLM_PROC_PATTERN="${BUDDY_RKLLM_PROC_PATTERN:-python3.*flask_server.py}"
RKLLM_VENV_DIR="$PROJECT_DIR/rkllm_server/.venv"
RKLLM_REQ_FILE="$PROJECT_DIR/rkllm_server/requirements.txt"

source "$SCRIPT_DIR/common.sh"

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

resolve_ollama_model() {
    if [[ -n "${BUDDY_OLLAMA_MODEL:-}" ]]; then
        echo "$BUDDY_OLLAMA_MODEL"
        return 0
    fi
    local local_llm_yaml=""
    if [[ -f "$PROJECT_DIR/params/local_llm.yaml" ]]; then
        local_llm_yaml="$PROJECT_DIR/params/local_llm.yaml"
    elif [[ -f "$PROJECT_DIR/src/buddy_app/params/local_llm.yaml" ]]; then
        local_llm_yaml="$PROJECT_DIR/src/buddy_app/params/local_llm.yaml"
    fi

    if [[ -n "$local_llm_yaml" ]]; then
        local local_model
        local_model="$(
            awk '
                /^local_llm:[[:space:]]*$/ {in_root=1; next}
                in_root && /^[^[:space:]].*:[[:space:]]*$/ {in_root=0}
                in_root && /^[[:space:]]+ros__parameters:[[:space:]]*$/ {in_param=1; next}
                in_param && /^[[:space:]]+[A-Za-z0-9_]+:[[:space:]]*$/ && $1 !~ /model_name:/ {next}
                in_param && /^[[:space:]]+model_name:[[:space:]]*/ {
                    value=$0; sub(/^[^:]*:[[:space:]]*/, "", value); gsub(/"/, "", value); print value; exit
                }
            ' "$local_llm_yaml" 2>/dev/null
        )"
        if [[ -n "$local_model" ]]; then
            echo "$local_model"
            return 0
        fi
    fi

    if [[ -f "$LLM_CONFIG" ]]; then
        local parsed
        parsed="$(
            awk '
                /^  backends:[[:space:]]*$/ {in_backends=1; next}
                in_backends && /^  [A-Za-z0-9_]+:[[:space:]]*$/ {in_backends=0}
                in_backends && /^    ollama:[[:space:]]*$/ {in_ollama=1; next}
                in_ollama && /^    [A-Za-z0-9_]+:[[:space:]]*$/ {in_ollama=0}
                in_ollama && /^      model:[[:space:]]*/ {
                    value=$0
                    sub(/^[^:]*:[[:space:]]*/, "", value)
                    gsub(/"/, "", value)
                    print value
                    exit
                }
            ' "$LLM_CONFIG"
        )"
        if [[ -n "$parsed" ]]; then
            echo "$parsed"
            return 0
        fi
    fi
    echo "qwen2.5:7b"
}

resolve_rkllm_url() {
    if [[ -n "${BUDDY_RKLLM_URL:-}" ]]; then
        echo "$BUDDY_RKLLM_URL"
        return 0
    fi

    local base_url="http://127.0.0.1:8080"
    local endpoint="/rkllm_chat"
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

resolve_rkllm_server_cmd() {
    if [[ -n "${BUDDY_RKLLM_SERVER_CMD:-}" ]]; then
        echo "$BUDDY_RKLLM_SERVER_CMD"
        return 0
    fi

    local flask_server=""
    local candidate1="$PROJECT_DIR/rkllm_server/flask_server.py"
    local candidate2="$PROJECT_DIR/docker/rkllm_server/flask_server.py"
    if [[ -f "$candidate1" ]]; then
        flask_server="$candidate1"
    elif [[ -f "$candidate2" ]]; then
        flask_server="$candidate2"
    else
        log_err "RKLLM server script not found"
        echo "  [!!] Set BUDDY_RKLLM_SERVER_CMD or place flask_server.py under:" >&2
        echo "       $PROJECT_DIR/rkllm_server/" >&2
        return 1
    fi

    local model_path="${BUDDY_RKLLM_MODEL_PATH:-}"
    if [[ -z "$model_path" && -d "$PROJECT_DIR/models/rkllm" ]]; then
        model_path="$(find "$PROJECT_DIR/models/rkllm" -type f -name '*.rkllm' 2>/dev/null | head -1 || true)"
    fi
    if [[ -z "$model_path" && -d "$PROJECT_DIR/models" ]]; then
        model_path="$(find "$PROJECT_DIR/models" -type f -name '*.rkllm' 2>/dev/null | head -1 || true)"
    fi
    if [[ -z "$model_path" || ! -f "$model_path" ]]; then
        log_err "RKLLM model not found"
        echo "  [!!] Place .rkllm files in models/rkllm/ or set BUDDY_RKLLM_MODEL_PATH" >&2
        return 1
    fi

    local platform="${BUDDY_RKLLM_TARGET_PLATFORM:-rk3588}"
    # flask_server.py uses relative path 'lib/librkllmrt.so', must cd to its directory
    local flask_dir
    flask_dir="$(dirname "$flask_server")"
    local cmd="cd \"$flask_dir\" && \"$RKLLM_VENV_DIR/bin/python3\" flask_server.py --rkllm_model_path \"$model_path\" --target_platform \"$platform\""
    if [[ -n "${BUDDY_RKLLM_LORA_MODEL_PATH:-}" ]]; then
        cmd="$cmd --lora_model_path \"${BUDDY_RKLLM_LORA_MODEL_PATH}\""
    fi
    if [[ -n "${BUDDY_RKLLM_PROMPT_CACHE_PATH:-}" ]]; then
        cmd="$cmd --prompt_cache_path \"${BUDDY_RKLLM_PROMPT_CACHE_PATH}\""
    fi
    echo "$cmd"
}

# ════════════════════════════════════════════════════════════════
# Ollama Management
# ════════════════════════════════════════════════════════════════

is_ollama_ready() {
    check_http "$OLLAMA_URL"
}

is_ollama_model_ready() {
    ollama list 2>/dev/null | grep -q "$OLLAMA_MODEL"
}

resolve_ollama_backend() {
    case "$OLLAMA_BACKEND" in
        auto)
            if has_nvidia_gpu; then echo "cuda_v12"; else echo "cpu"; fi
            ;;
        cpu|cuda_v12) echo "$OLLAMA_BACKEND" ;;
        *) log_err "Invalid OLLAMA_LLM_LIBRARY=$OLLAMA_BACKEND (use auto|cpu|cuda_v12)"; return 1 ;;
    esac
}

check_cuda_backend_access() {
    local backend_dir="/usr/local/lib/ollama/cuda_v12"
    if [ ! -d "$backend_dir" ]; then
        log_err "CUDA backend dir missing: $backend_dir"
        return 1
    fi
    if [ ! -x "$backend_dir" ] || [ ! -r "$backend_dir" ]; then
        log_err "No permission for $backend_dir (ollama will fallback to CPU)"
        echo "  [!!] Fix: sudo chmod -R a+rX /usr/local/lib/ollama/cuda_v12" >&2
        return 1
    fi
    return 0
}

install_ollama() {
    if command -v ollama &>/dev/null; then
        log_skip "Ollama binary"
        return 0
    fi
    log_step "Installing ollama ..."
    curl -fsSL https://ollama.com/install.sh | sh
    log_ok "Ollama binary installed"
}

wait_for_ollama() {
    local timeout_msg="$1"
    local saved="$PROC_PATTERN"
    PROC_PATTERN="$OLLAMA_PROC_PATTERN"
    if wait_for_ready "check_http $OLLAMA_URL" 60 "Ollama"; then
        log_ok "Ollama server ready"
        PROC_PATTERN="$saved"
        return 0
    fi
    PROC_PATTERN="$saved"
    log_err "$timeout_msg"
    return 1
}

start_ollama() {
    if is_ollama_ready; then
        log_skip "Ollama server (already running)"
        return 0
    fi

    if is_proc_running "$OLLAMA_PROC_PATTERN"; then
        log_step "Waiting for existing Ollama process..."
        wait_for_ollama "Timeout waiting for existing Ollama"
        return $?
    fi

    local backend
    backend="$(resolve_ollama_backend)"
    if [ "$backend" = "cuda_v12" ]; then
        log_step "GPU detected, target backend=$backend"
        check_cuda_backend_access || backend="cpu"
    else
        log_step "No NVIDIA GPU detected, using CPU backend"
    fi

    log_step "Starting Ollama server (backend=$backend) ..."
    : > "$OLLAMA_LOG"
    OLLAMA_LLM_LIBRARY="$backend" nohup ollama serve > "$OLLAMA_LOG" 2>&1 &
    wait_for_ollama "Timeout starting Ollama. Check $OLLAMA_LOG"
}

pull_ollama_model() {
    if is_ollama_model_ready; then
        log_skip "Model $OLLAMA_MODEL"
        return 0
    fi
    log_step "Pulling model $OLLAMA_MODEL (~2-3GB) ..."
    ollama pull "$OLLAMA_MODEL"
    log_ok "Model $OLLAMA_MODEL ready"
}

stop_ollama() {
    do_stop "$OLLAMA_PROC_PATTERN"
}

# ════════════════════════════════════════════════════════════════
# RKLLM Management
# ════════════════════════════════════════════════════════════════

is_rkllm_ready() {
    local base_url
    base_url="$(resolve_rkllm_url)"
    base_url="${base_url%/rkllm_chat}"
    check_http "${base_url}/health"
}

wait_for_rkllm() {
    local timeout_msg="$1"
    local saved="$PROC_PATTERN"
    PROC_PATTERN="$RKLLM_PROC_PATTERN"
    if wait_for_ready "is_rkllm_ready" 120 "RKLLM server"; then
        log_ok "RKLLM server ready"
        PROC_PATTERN="$saved"
        return 0
    fi
    PROC_PATTERN="$saved"
    log_err "$timeout_msg"
    return 1
}

start_rkllm() {
    if is_rkllm_ready; then
        log_skip "RKLLM server (already running)"
        return 0
    fi

    if is_proc_running "$RKLLM_PROC_PATTERN"; then
        log_step "Waiting for existing RKLLM server process..."
        wait_for_rkllm "Timeout waiting for existing RKLLM server"
        return $?
    fi

    # Ensure RKLLM venv exists and has dependencies
    ensure_venv "$RKLLM_VENV_DIR" "$RKLLM_REQ_FILE"

    local rkllm_cmd
    rkllm_cmd="$(resolve_rkllm_server_cmd)" || return 1

    log_step "Starting RKLLM server ..."
    : > "$RKLLM_LOG"
    nohup env LD_LIBRARY_PATH="$BUDDY_LD_LIBRARY_PATH" bash -lc "$rkllm_cmd" > "$RKLLM_LOG" 2>&1 &
    wait_for_rkllm "Timeout starting RKLLM server. Check $RKLLM_LOG"
}

stop_rkllm() {
    do_stop "$RKLLM_PROC_PATTERN"
}

ensure_local_backend_runtime() {
    local local_backend="$1"
    case "$local_backend" in
        ollama)
            install_ollama
            start_ollama
            pull_ollama_model
            ;;
        rk_llm)
            if [[ "${BUDDY_RKLLM_ON_DEMAND:-1}" == "1" ]]; then
                if resolve_rkllm_server_cmd >/dev/null 2>&1; then
                    log_skip "RKLLM server (on-demand)"
                else
                    log_err "RKLLM model not found or server config invalid"
                    resolve_rkllm_server_cmd >/dev/null
                    return 1
                fi
            else
                start_rkllm
            fi
            ;;
        *)
            log_skip "No managed runtime for backend=$local_backend"
            ;;
    esac
}

# ════════════════════════════════════════════════════════════════
# Python LLM Service
# ════════════════════════════════════════════════════════════════

is_llm_running() {
    is_proc_running "$PROC_PATTERN"
}

start_llm_service() {
    if is_llm_running; then
        log_ok "LLM server already running"
        return 0
    fi

    ensure_venv "$VENV_DIR" "$SERVICE_DIR/requirements.txt"

    log_step "Starting LLM server on :$LLM_PORT ..."
    : > "$LLM_LOG"
    nohup env LD_LIBRARY_PATH="$BUDDY_LD_LIBRARY_PATH" python3 "$SERVICE_DIR/server.py" > "$LLM_LOG" 2>&1 &

    if wait_for_ready "check_http http://127.0.0.1:$LLM_PORT/ready" 30 "LLM server"; then
        log_ok "LLM server ready"
        return 0
    fi
    log_err "LLM server failed to start. Check $LLM_LOG"
    return 1
}

stop_llm_service() {
    do_stop "$PROC_PATTERN"
}

# ════════════════════════════════════════════════════════════════
# Main
# ════════════════════════════════════════════════════════════════

OLLAMA_MODEL="${OLLAMA_MODEL:-$(resolve_ollama_model)}"

case "${1:-start}" in
    start-rkllm)
        log_stage "RKLLM Service"
        start_rkllm
        exit 0
        ;;
    stop-rkllm)
        stop_rkllm
        exit 0
        ;;
    stop)
        stop_llm_service
        stop_ollama
        stop_rkllm
        exit 0
        ;;
    restart)
        local_backend="$(resolve_local_backend_mode)"
        ensure_local_backend_runtime "$local_backend"
        stop_llm_service
        sleep 1
        start_llm_service
        echo ""
        echo "  LLM API: http://127.0.0.1:$LLM_PORT"
        exit 0
        ;;
    install)
        log_stage "LLM Service (install-only)"
        local_backend="$(resolve_local_backend_mode)"
        ensure_local_backend_runtime "$local_backend"
        ensure_venv "$VENV_DIR" "$SERVICE_DIR/requirements.txt"
        log_ok "Install complete"
        exit 0
        ;;
    -f)
        log_stage "LLM Service (foreground)"
        local_backend="$(resolve_local_backend_mode)"
        ensure_local_backend_runtime "$local_backend"
        ensure_venv "$VENV_DIR" "$SERVICE_DIR/requirements.txt"
        log_step "Starting LLM server in foreground on :$LLM_PORT"
        export LD_LIBRARY_PATH="$BUDDY_LD_LIBRARY_PATH"
        exec python3 "$SERVICE_DIR/server.py"
        ;;
    start)
        log_stage "Unified LLM Service"
        local_backend="$(resolve_local_backend_mode)"
        log_step "Resolved local backend: $local_backend"
        # Phase 1: Local backend runtime
        ensure_local_backend_runtime "$local_backend"
        # Phase 2: Python FastAPI service
        start_llm_service
        echo ""
        if [[ "$local_backend" == "ollama" ]]; then
            echo "  Ollama: $OLLAMA_URL  |  LLM API: http://127.0.0.1:$LLM_PORT"
        elif [[ "$local_backend" == "rk_llm" ]]; then
            echo "  RKLLM: $(resolve_rkllm_url)  |  LLM API: http://127.0.0.1:$LLM_PORT"
        else
            echo "  Local backend: $local_backend  |  LLM API: http://127.0.0.1:$LLM_PORT"
        fi
        ;;
    *)
        echo "Usage: $0 [start|stop|restart|install|-f|start-rkllm|stop-rkllm]"
        exit 1
        ;;
esac
