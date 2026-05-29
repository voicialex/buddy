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

# ── Ollama config ──
OLLAMA_MODEL_DIR="$PROJECT_DIR/models/ollama"
OLLAMA_LOG="/tmp/buddy-ollama.log"
OLLAMA_MODEL="buddy"
OLLAMA_URL="http://localhost:11434"
OLLAMA_BACKEND="${OLLAMA_LLM_LIBRARY:-auto}"  # auto | cpu | cuda_v12

# ── Python LLM service config ──
SERVICE_DIR="$PROJECT_DIR/services/llm"
VENV_DIR="$SERVICE_DIR/.venv"
LLM_LOG="/tmp/buddy-llm-server.log"
LLM_PORT=8002
PROC_PATTERN="python.*services/llm/server.py"
OLLAMA_PROC_PATTERN="ollama serve"

source "$SCRIPT_DIR/common.sh"

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
    mkdir -p "$OLLAMA_MODEL_DIR"
    export OLLAMA_MODELS="$OLLAMA_MODEL_DIR"
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
    nohup python3 "$SERVICE_DIR/server.py" > "$LLM_LOG" 2>&1 &

    if wait_for_ready "check_http http://127.0.0.1:$LLM_PORT/health" 15 "LLM server"; then
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

case "${1:-start}" in
    stop)
        stop_llm_service
        stop_ollama
        exit 0
        ;;
    restart)
        stop_llm_service
        sleep 1
        start_llm_service
        echo ""
        echo "  LLM API: http://127.0.0.1:$LLM_PORT"
        exit 0
        ;;
    install)
        log_stage "LLM Service (install-only)"
        install_ollama
        mkdir -p "$OLLAMA_MODEL_DIR"
        export OLLAMA_MODELS="$OLLAMA_MODEL_DIR"
        start_ollama
        pull_ollama_model
        ensure_venv "$VENV_DIR" "$SERVICE_DIR/requirements.txt"
        log_ok "Install complete"
        exit 0
        ;;
    -f)
        log_stage "LLM Service (foreground)"
        install_ollama
        mkdir -p "$OLLAMA_MODEL_DIR"
        export OLLAMA_MODELS="$OLLAMA_MODEL_DIR"
        start_ollama
        pull_ollama_model
        ensure_venv "$VENV_DIR" "$SERVICE_DIR/requirements.txt"
        log_step "Starting LLM server in foreground on :$LLM_PORT"
        exec python3 "$SERVICE_DIR/server.py"
        ;;
    start)
        log_stage "Unified LLM Service"
        # Phase 1: Ollama backend
        install_ollama
        mkdir -p "$OLLAMA_MODEL_DIR"
        export OLLAMA_MODELS="$OLLAMA_MODEL_DIR"
        start_ollama
        pull_ollama_model
        # Phase 2: Python FastAPI service
        start_llm_service
        echo ""
        echo "  Ollama: $OLLAMA_URL  |  LLM API: http://127.0.0.1:$LLM_PORT"
        ;;
    *)
        echo "Usage: $0 [start|stop|restart|install|-f]"
        exit 1
        ;;
esac
