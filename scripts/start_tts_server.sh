#!/usr/bin/env bash
# ChatTTS 一键部署与启动脚本
#
# 用法:
#   ./scripts/start_tts_server.sh          # 后台运行
#   ./scripts/start_tts_server.sh -f       # 前台运行（可看到日志）
#   ./scripts/start_tts_server.sh restart  # 重启服务
#   ./scripts/start_tts_server.sh install  # 仅安装依赖，不启动
#   ./scripts/start_tts_server.sh stop     # 停止服务
#
# 模型存放: src/buddy_audio/models/ChatTTS/
# 服务地址: http://127.0.0.1:9880/tts
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVICE_DIR="$PROJECT_DIR/services/tts"
VENV_DIR="$SERVICE_DIR/.venv"
MODEL_DIR="$PROJECT_DIR/src/buddy_audio/models/ChatTTS"
LOG_FILE="/tmp/buddy-tts.log"
TTS_URL="http://127.0.0.1:9880/docs"
WAIT_INTERVAL_SEC=2
WAIT_TIMEOUT_SEC="${CHAT_TTS_WAIT_TIMEOUT_SEC:-1800}"
TORCH_VARIANT="${CHAT_TTS_TORCH_VARIANT:-auto}"  # auto | cpu | cu126 | cu128
PROC_PATTERN="python.*services/tts/server.py"

cd "$PROJECT_DIR"

source "$SCRIPT_DIR/common.sh"

# ── 各步骤函数 ──

is_server_ready() {
    check_http "$TTS_URL"
}

create_venv() {
    if [ -f "$VENV_DIR/bin/activate" ]; then
        log_skip "Python venv"
        return 0
    fi
    log_step "Creating venv at $VENV_DIR ..."
    python3 -m venv --system-site-packages "$VENV_DIR"
    log_ok "Python venv created"
}

resolve_torch_mode() {
    # If previously fell back to CPU, remember it
    if [[ -f "$VENV_DIR/.torch-cpu-fallback" ]]; then
        echo "cpu"
        return
    fi
    case "$TORCH_VARIANT" in
        auto)
            if has_nvidia_gpu; then
                echo "cu128"
            else
                echo "cpu"
            fi
            ;;
        cpu|cu126|cu128)
            echo "$TORCH_VARIANT"
            ;;
        *)
            log_err "Invalid CHAT_TTS_TORCH_VARIANT=$TORCH_VARIANT (use auto|cpu|cu126|cu128)"
            return 1
            ;;
    esac
}

torch_probe() {
    local mode="$1"  # mode | backend
    python - "$mode" <<'PY'
import importlib.util
import sys

mode = sys.argv[1]
if importlib.util.find_spec("torch") is None:
    print("none" if mode == "mode" else "unknown")
    sys.exit(0)

try:
    import torch
except Exception:
    print("broken")
    sys.exit(0)

if mode == "mode":
    if "+cpu" in torch.__version__:
        print("cpu")
    elif torch.version.cuda:
        v = torch.version.cuda.split(".")
        major = v[0] if len(v) > 0 else ""
        minor = v[1] if len(v) > 1 else ""
        if major and minor:
            print(f"cu{major}{minor}")
        else:
            print("cu")
    else:
        print("unknown")
else:
    if torch.cuda.is_available():
        name = torch.cuda.get_device_name(0) if torch.cuda.device_count() > 0 else "CUDA"
        print(f"GPU ({name})")
    else:
        print("CPU")
PY
}

current_torch_mode() {
    torch_probe mode
}

install_torch() {
    local mode="$1"
    local torch_index
    case "$mode" in
        cu126) torch_index="https://download.pytorch.org/whl/cu126" ;;
        cu128) torch_index="https://download.pytorch.org/whl/cu128" ;;
        cpu) torch_index="https://download.pytorch.org/whl/cpu" ;;
        *)
            log_err "Unsupported torch mode: $mode"
            return 1
            ;;
    esac
    log_step "Installing torch ($mode) ..."
    pip install --upgrade pip
    pip uninstall -y torch torchaudio >/dev/null 2>&1 || true
    pip install torch --index-url "$torch_index"
    log_ok "torch ($mode)"
}

validate_cuda_runtime() {
    python - <<'PY'
import sys
import torch
if not torch.cuda.is_available():
    print("cuda_not_available")
    sys.exit(1)
try:
    _ = torch.randn(4, device="cuda")
except Exception as e:
    print(f"cuda_runtime_error:{e}")
    sys.exit(2)
print("cuda_ok")
PY
}

report_runtime_backend() {
    local runtime
    runtime="$(torch_probe backend)"
    log_step "ChatTTS runtime backend: $runtime"
}

install_deps() {
    source "$VENV_DIR/bin/activate"

    local target_mode current_mode
    target_mode="$(resolve_torch_mode)"
    current_mode="$(current_torch_mode)"
    if [[ "$target_mode" == cu* ]]; then
        log_step "GPU probe: NVIDIA GPU detected, target torch=$target_mode"
    else
        log_step "GPU probe: no NVIDIA GPU detected, target torch=cpu"
    fi

    if [ "$current_mode" = "none" ] || [ "$current_mode" = "broken" ]; then
        if [ "$current_mode" = "broken" ]; then
            log_step "Existing torch install is broken, reinstall to $target_mode"
        fi
        install_torch "$target_mode"
    elif [[ "$target_mode" == cu* ]] && [ "$current_mode" != "$target_mode" ]; then
        log_step "Existing torch runtime is $current_mode, reinstall to $target_mode"
        install_torch "$target_mode"
    elif [ "$target_mode" = "cpu" ] && [ "$current_mode" != "cpu" ]; then
        log_step "Existing torch runtime is $current_mode, reinstall to cpu"
        install_torch "$target_mode"
    else
        log_skip "torch ($current_mode)"
    fi

    if [[ "$target_mode" == cu* ]]; then
        local cuda_probe
        cuda_probe="$(validate_cuda_runtime 2>&1 || true)"
        if [[ "$cuda_probe" == "cuda_ok" ]]; then
            log_ok "CUDA runtime check passed"
        else
            log_err "CUDA runtime check failed: $cuda_probe"
            log_step "Fallback to CPU torch to keep ChatTTS service available"
            touch "$VENV_DIR/.torch-cpu-fallback"
            install_torch "cpu"
        fi
    fi

    log_step "Installing ChatTTS dependencies ..."
    pip install --progress-bar on -r "$SERVICE_DIR/requirements.txt"
    log_ok "ChatTTS dependencies"
}

start_server_bg() {
    if is_server_ready; then
        log_skip "ChatTTS server (already running)"
        return 0
    fi

    if is_proc_running; then
        log_skip "ChatTTS process starting (waiting for readiness)"
    else
        log_step "Starting ChatTTS server ..."
        export CHAT_TTS_MODELS="$MODEL_DIR"
        : > "$LOG_FILE"
        nohup python "$SERVICE_DIR/server.py" >> "$LOG_FILE" 2>&1 &
    fi

    local wait_timeout_sec="$WAIT_TIMEOUT_SEC"
    if ! [[ "$wait_timeout_sec" =~ ^[0-9]+$ ]] || [ "$wait_timeout_sec" -le 0 ]; then
        wait_timeout_sec=1800
    fi
    local max_checks=$(((wait_timeout_sec + WAIT_INTERVAL_SEC - 1) / WAIT_INTERVAL_SEC))

    stream_new_log_lines() {
        local from_line="$1"
        if [ ! -f "$LOG_FILE" ]; then
            echo "$from_line"
            return 0
        fi
        local total
        total=$(wc -l < "$LOG_FILE")
        if [ "$total" -gt "$from_line" ]; then
            sed -n "$((from_line + 1)),${total}p" "$LOG_FILE" | sed 's/^/  /'
        fi
        echo "$total"
    }

    # Stream log output while waiting for server (shows download progress)
    local last_line=0
    for i in $(seq 1 "$max_checks"); do
        last_line="$(stream_new_log_lines "$last_line")"

        if is_server_ready; then
            log_ok "ChatTTS server ready (took $((i * WAIT_INTERVAL_SEC))s)"
            return 0
        fi
        if ! is_proc_running; then
            last_line="$(stream_new_log_lines "$last_line")"
            log_err "Server process died. Check $LOG_FILE"
            return 1
        fi
        sleep "$WAIT_INTERVAL_SEC"
    done
    log_err "Timeout waiting for server after ${wait_timeout_sec}s. First run may still be downloading models in background. Check $LOG_FILE"
    return 1
}

# ── Main ──

case "${1:-start}" in
    stop)
        do_stop
        exit 0
        ;;
    restart)
        do_stop
        sleep 1
        log_stage "ChatTTS Server"
        create_venv
        install_deps
        report_runtime_backend
        mkdir -p "$MODEL_DIR"
        start_server_bg
        log_ok "ChatTTS server running"
        echo "        URL: http://127.0.0.1:9880  |  Log: $LOG_FILE"
        exit 0
        ;;
    install)
        log_stage "ChatTTS Server (install-only)"
        create_venv
        install_deps
        report_runtime_backend
        log_ok "Install complete"
        exit 0
        ;;
    -f)
        log_stage "ChatTTS Server (foreground)"
        create_venv
        install_deps
        report_runtime_backend
        mkdir -p "$MODEL_DIR"
        export CHAT_TTS_MODELS="$MODEL_DIR"
        log_step "Starting in foreground on http://127.0.0.1:9880"
        exec python "$SERVICE_DIR/server.py"
        ;;
    start)
        if is_server_ready; then
            log_ok "ChatTTS already running at http://127.0.0.1:9880"
            exit 0
        fi
        log_stage "ChatTTS Server"
        create_venv
        install_deps
        report_runtime_backend
        mkdir -p "$MODEL_DIR"
        start_server_bg
        log_ok "ChatTTS server running"
        echo "        URL: http://127.0.0.1:9880  |  Log: $LOG_FILE"
        ;;
    *)
        echo "Usage: $0 [start|stop|restart|install|-f]"
        echo "  start     Start server in background (default)"
        echo "  stop      Stop running server"
        echo "  restart   Stop then start"
        echo "  install   Install dependencies only"
        echo "  -f        Run in foreground"
        exit 1
        ;;
esac
