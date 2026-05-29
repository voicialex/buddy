#!/usr/bin/env bash
# FunASR WebSocket Server launcher (deb deployment, 2pass streaming)
# Usage: start_asr_server.sh [start|stop|-f]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUDDY_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

FUNASR_BIN="$BUDDY_DIR/bin/funasr-wss-server-2pass"
OFFLINE_MODEL="$BUDDY_DIR/models/funasr-paraformer-zh-offline"
ONLINE_MODEL="$BUDDY_DIR/models/funasr-paraformer-zh-online"
VAD_MODEL="$BUDDY_DIR/models/funasr-vad"
ONNXRT_LIB="$BUDDY_DIR/lib/funasr"

LOG_FILE="/tmp/funasr-server.log"
PID_FILE="/tmp/funasr-server.pid"
PORT=10095
NUM_THREADS=4

# --- Helpers ---
log_ok()   { echo "  [ok] $1"; }
log_step() { echo "  [..] $1"; }
log_err()  { echo "  [!!] $1"; }

is_server_ready() {
    ss -tlnp 2>/dev/null | grep -q ":${PORT} " 2>/dev/null
}

do_stop() {
    if [[ -f "$PID_FILE" ]]; then
        local pid; pid=$(cat "$PID_FILE")
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid"; rm -f "$PID_FILE"
            log_ok "FunASR stopped (PID $pid)"
        else
            rm -f "$PID_FILE"
        fi
    fi
}

check_models() {
    local missing=()
    [[ ! -f "$OFFLINE_MODEL/model_quant.onnx" ]] && missing+=("offline: $OFFLINE_MODEL")
    [[ ! -f "$ONLINE_MODEL/model_quant.onnx" ]] && missing+=("online: $ONLINE_MODEL")
    [[ ! -f "$VAD_MODEL/model_quant.onnx" ]] && missing+=("vad: $VAD_MODEL")
    if [[ ${#missing[@]} -gt 0 ]]; then
        log_err "Missing FunASR models:"
        for m in "${missing[@]}"; do
            echo "         - $m"
        done
        echo ""
        echo "       Deploy models tarball: tar xf buddy-models_*.tar -C $BUDDY_DIR/"
        return 1
    fi
}

start_server() {
    if is_server_ready; then
        log_ok "FunASR already running on ws://127.0.0.1:$PORT"
        return 0
    fi

    if [[ ! -x "$FUNASR_BIN" ]]; then
        log_err "Binary not found: $FUNASR_BIN"
        return 1
    fi
    check_models || return 1

    # Check runtime library dependencies
    local check_output
    check_output=$(LD_LIBRARY_PATH="$ONNXRT_LIB:$BUDDY_DIR/lib:${LD_LIBRARY_PATH:-}" ldd "$FUNASR_BIN" 2>&1 | grep "not found" || true)
    if [[ -n "$check_output" ]]; then
        log_err "Missing shared libraries:"
        echo "$check_output" | while IFS= read -r line; do
            printf '         %s\n' "$line"
        done
        echo ""
        echo "       Fix: sudo apt install libavformat-dev libavcodec-dev libavutil-dev"
        echo "        or: sudo apt install ffmpeg"
        return 1
    fi

    log_step "Starting FunASR 2pass on ws://127.0.0.1:$PORT ..."
    : > "$LOG_FILE"

    LD_LIBRARY_PATH="$ONNXRT_LIB:$BUDDY_DIR/lib:${LD_LIBRARY_PATH:-}" \
    nohup "$FUNASR_BIN" \
        --model-dir "$OFFLINE_MODEL" \
        --online-model-dir "$ONLINE_MODEL" \
        --vad-dir "$VAD_MODEL" \
        --punc-dir "" \
        --itn-dir "" \
        --lm-dir "" \
        --port "$PORT" \
        --decoder-thread-num "$NUM_THREADS" \
        >> "$LOG_FILE" 2>&1 &
    echo "$!" > "$PID_FILE"

    # Wait for ready (max 60s — 2pass loads 3 models)
    for i in $(seq 1 60); do
        if is_server_ready; then
            log_ok "FunASR ready (PID $(cat "$PID_FILE"), took ${i}s)"
            return 0
        fi
        if ! kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
            log_err "Process died. Check $LOG_FILE"
            return 1
        fi
        sleep 1
    done
    log_err "Timeout after 60s. Check $LOG_FILE"
    return 1
}

# --- Main ---
case "${1:-start}" in
    stop) do_stop ;;
    -f)
        [[ -x "$FUNASR_BIN" ]] || { log_err "Binary not found: $FUNASR_BIN"; exit 1; }
        check_models || exit 1
        log_step "FunASR 2pass foreground on ws://127.0.0.1:$PORT"
        LD_LIBRARY_PATH="$ONNXRT_LIB:$BUDDY_DIR/lib:${LD_LIBRARY_PATH:-}" \
        exec "$FUNASR_BIN" \
            --model-dir "$OFFLINE_MODEL" \
            --online-model-dir "$ONLINE_MODEL" \
            --vad-dir "$VAD_MODEL" \
            --punc-dir "" --itn-dir "" --lm-dir "" \
            --port "$PORT" --decoder-thread-num "$NUM_THREADS"
        ;;
    start|"")
        start_server
        ;;
    *) echo "Usage: $0 [start|stop|-f]"; exit 1 ;;
esac
