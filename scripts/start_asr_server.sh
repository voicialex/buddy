#!/usr/bin/env bash
# FunASR C++ WebSocket Server 一键编译与启动脚本
#
# 用法:
#   ./scripts/start_asr_server.sh           # 后台启动
#   ./scripts/start_asr_server.sh -f        # 前台运行
#   ./scripts/start_asr_server.sh build     # 仅编译，不启动
#   ./scripts/start_asr_server.sh restart   # 重启服务
#   ./scripts/start_asr_server.sh stop      # 停止服务
#
# 依赖: prebuilt/<arch>/onnxruntime (由 setup_prebuilt.sh 安装)
# 模型: src/buddy_audio/models/funasr-paraformer-zh/
# 服务: ws://127.0.0.1:10095
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Detect architecture and ensure prebuilt/current symlink
case "$(uname -m)" in
    aarch64|arm64) ARCH="aarch64" ;;
    *)             ARCH="x86_64" ;;
esac
ln -sfn "$ARCH" "$PROJECT_DIR/prebuilt/current"

FUNASR_SRC="$PROJECT_DIR/third_party/funasr-runtime"
FUNASR_BIN="$PROJECT_DIR/prebuilt/current/funasr/bin/funasr-wss-server-2pass"
ONNXRT_DIR="$PROJECT_DIR/prebuilt/current/onnxruntime"
OFFLINE_MODEL="$PROJECT_DIR/models/funasr-paraformer-zh-offline"
ONLINE_MODEL="$PROJECT_DIR/models/funasr-paraformer-zh-online"
VAD_MODEL="$PROJECT_DIR/models/funasr-vad"

LOG_FILE="/tmp/buddy-asr.log"
PORT=10095
NUM_THREADS=4
PROC_PATTERN="funasr-wss-server.*--port $PORT"

cd "$PROJECT_DIR"
source "$SCRIPT_DIR/common.sh"

# ── 函数 ──

is_server_ready() {
    check_port "$PORT"
}

ensure_binary() {
    if [ -f "$FUNASR_BIN" ]; then
        return 0
    fi
    log_step "FunASR binary not found, building ..."
    "$SCRIPT_DIR/build_thirdparty.sh"
    if [ ! -f "$FUNASR_BIN" ]; then
        log_err "Build failed — FunASR binary still missing"
        return 1
    fi
    log_ok "FunASR binary ready"
}

download_model() {
    local missing=0

    # Offline model
    if [ ! -f "$OFFLINE_MODEL/model_quant.onnx" ]; then
        log_step "Downloading FunASR offline model ..."
        mkdir -p "$OFFLINE_MODEL"
        if command -v modelscope >/dev/null 2>&1; then
            modelscope download --model iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-onnx \
                --local_dir "$OFFLINE_MODEL"
        else
            log_err "modelscope CLI not found. Install: pip install modelscope"
            echo "       Model: iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-onnx"
            missing=1
        fi
    else
        log_skip "FunASR offline model"
    fi

    # Online streaming model
    if [ ! -f "$ONLINE_MODEL/model_quant.onnx" ]; then
        log_step "Downloading FunASR online model ..."
        mkdir -p "$ONLINE_MODEL"
        if command -v modelscope >/dev/null 2>&1; then
            modelscope download --model iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-online-onnx \
                --local_dir "$ONLINE_MODEL"
        else
            log_err "modelscope CLI not found. Install: pip install modelscope"
            echo "       Model: iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-online-onnx"
            missing=1
        fi
    else
        log_skip "FunASR online model"
    fi

    # VAD model
    if [ ! -f "$VAD_MODEL/model_quant.onnx" ]; then
        log_step "Downloading FunASR VAD model ..."
        mkdir -p "$VAD_MODEL"
        if command -v modelscope >/dev/null 2>&1; then
            modelscope download --model iic/speech_fsmn_vad_zh-cn-16k-common-onnx \
                --local_dir "$VAD_MODEL"
        else
            log_err "modelscope CLI not found. Install: pip install modelscope"
            echo "       Model: iic/speech_fsmn_vad_zh-cn-16k-common-onnx"
            missing=1
        fi
    else
        log_skip "FunASR VAD model"
    fi

    [[ "$missing" -eq 0 ]] || return 1
    log_ok "All FunASR models ready"
}

start_server_bg() {
    if is_server_ready; then
        log_skip "FunASR server (already running on port $PORT)"
        return 0
    fi

    if is_proc_running; then
        log_skip "FunASR process starting (waiting for readiness)"
    else
        if [ ! -f "$FUNASR_BIN" ]; then
            log_err "FunASR binary not found. Run: $0 build"
            return 1
        fi

        log_step "Starting FunASR 2pass server on port $PORT ..."
        : > "$LOG_FILE"

        LD_LIBRARY_PATH="${PROJECT_DIR}/prebuilt/current/funasr/lib:${ONNXRT_DIR}/lib:${LD_LIBRARY_PATH:-}" \
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
    fi

    wait_for_ready "check_port $PORT" 30 "FunASR" || return 1
    log_ok "FunASR server ready (port $PORT)"
}

# ── Main ──

case "${1:-start}" in
    stop)
        do_stop
        ;;
    restart)
        do_stop
        sleep 1
        log_stage "FunASR WebSocket Server"
        ensure_binary
        download_model
        start_server_bg
        echo "        URL: ws://127.0.0.1:$PORT  |  Log: $LOG_FILE"
        ;;
    build)
        log_stage "FunASR C++ Runtime Build"
        "$SCRIPT_DIR/build_thirdparty.sh"
        ;;
    -f)
        log_stage "FunASR Server (foreground)"
        ensure_binary
        download_model
        log_step "Starting on ws://127.0.0.1:$PORT"
        LD_LIBRARY_PATH="${PROJECT_DIR}/prebuilt/current/funasr/lib:${ONNXRT_DIR}/lib:${LD_LIBRARY_PATH:-}" \
        exec "$FUNASR_BIN" \
            --model-dir "$OFFLINE_MODEL" \
            --online-model-dir "$ONLINE_MODEL" \
            --vad-dir "$VAD_MODEL" \
            --punc-dir "" \
            --itn-dir "" \
            --lm-dir "" \
            --port "$PORT" \
            --decoder-thread-num "$NUM_THREADS"
        ;;
    start|"")
        if is_server_ready; then
            log_ok "FunASR already running at ws://127.0.0.1:$PORT"
            exit 0
        fi
        log_stage "FunASR WebSocket Server"
        ensure_binary
        download_model
        start_server_bg
        echo "        URL: ws://127.0.0.1:$PORT  |  Log: $LOG_FILE"
        ;;
    *)
        echo "Usage: $0 [start|stop|restart|build|-f]"
        echo "  start     Start server in background (default)"
        echo "  stop      Stop running server"
        echo "  restart   Stop then start"
        echo "  build     Clone and compile only"
        echo "  -f        Run in foreground"
        exit 1
        ;;
esac
