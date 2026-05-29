#!/usr/bin/env bash
# ChatTTS Server launcher (deb deployment)
# Usage: start_tts_server.sh [start|stop|install|-f]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUDDY_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

VENV_DIR="$BUDDY_DIR/services/tts/.venv"
SERVICE_DIR="$BUDDY_DIR/services/tts"
MODEL_DIR="$BUDDY_DIR/models/ChatTTS"
TTS_SCRIPT="$SERVICE_DIR/server.py"
REQUIREMENTS="$SERVICE_DIR/requirements.txt"

LOG_FILE="/tmp/chattts.log"
PID_FILE="/tmp/chattts.pid"
TTS_CHECK_URL="http://127.0.0.1:9880/docs"
PORT=9880

# --- Helpers ---
log_ok()   { echo "  [ok] $1"; }
log_step() { echo "  [..] $1"; }
log_err()  { echo "  [!!] $1"; }
log_skip() { echo "  [--] $1 (already done)"; }

is_server_ready() {
    curl -sf -o /dev/null "$TTS_CHECK_URL" 2>/dev/null
}

do_stop() {
    if [[ -f "$PID_FILE" ]]; then
        local pid; pid=$(cat "$PID_FILE")
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid"; rm -f "$PID_FILE"
            log_ok "ChatTTS stopped (PID $pid)"
        else
            rm -f "$PID_FILE"
        fi
    fi
}

create_venv() {
    if [[ -f "$VENV_DIR/bin/activate" ]]; then
        log_skip "Python venv"
        return 0
    fi
    log_step "Creating venv at $VENV_DIR ..."
    mkdir -p "$(dirname "$VENV_DIR")"
    python3 -m venv "$VENV_DIR"
    log_ok "Python venv created"
}

install_deps() {
    source "$VENV_DIR/bin/activate"

    # Detect torch mode
    local target_mode="cpu"
    if command -v nvidia-smi &>/dev/null && nvidia-smi -L &>/dev/null; then
        target_mode="cu128"
    fi

    # Install torch if missing
    if ! python -c "import torch" 2>/dev/null; then
        local torch_index
        case "$target_mode" in
            cu128) torch_index="https://download.pytorch.org/whl/cu128" ;;
            *)     torch_index="https://download.pytorch.org/whl/cpu" ;;
        esac
        log_step "Installing torch ($target_mode) ..."
        pip install --upgrade pip
        pip install --progress-bar on torch torchaudio --index-url "$torch_index"
        log_ok "torch ($target_mode)"
    else
        log_skip "torch"
    fi

    log_step "Installing ChatTTS dependencies ..."
    pip install --progress-bar on -r "$REQUIREMENTS"
    log_ok "ChatTTS dependencies"
}

start_server() {
    if is_server_ready; then
        log_ok "ChatTTS already running at http://127.0.0.1:$PORT"
        return 0
    fi

    source "$VENV_DIR/bin/activate"
    export CHAT_TTS_MODELS="$MODEL_DIR"
    mkdir -p "$MODEL_DIR"

    log_step "Starting ChatTTS on http://127.0.0.1:$PORT ..."
    : > "$LOG_FILE"
    nohup python "$TTS_SCRIPT" >> "$LOG_FILE" 2>&1 &
    echo "$!" > "$PID_FILE"

    # Wait (ChatTTS loads model, can take 30-60s on arm64)
    local max_wait=120
    for i in $(seq 1 "$max_wait"); do
        if is_server_ready; then
            log_ok "ChatTTS ready (PID $(cat "$PID_FILE"), took ${i}s)"
            return 0
        fi
        if ! kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
            log_err "Process died. Check $LOG_FILE"
            return 1
        fi
        sleep 2
    done
    log_err "Timeout after $((max_wait*2))s (model may still be downloading). Check $LOG_FILE"
    return 1
}

# --- Main ---
case "${1:-start}" in
    stop) do_stop ;;
    install)
        create_venv
        install_deps
        log_ok "Install complete"
        ;;
    -f)
        source "$VENV_DIR/bin/activate"
        export CHAT_TTS_MODELS="$MODEL_DIR"
        log_step "ChatTTS foreground on http://127.0.0.1:$PORT"
        exec python "$TTS_SCRIPT"
        ;;
    start|"")
        create_venv
        install_deps
        start_server
        ;;
    *) echo "Usage: $0 [start|stop|install|-f]"; exit 1 ;;
esac
