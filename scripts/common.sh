#!/usr/bin/env bash
# scripts/ 下脚本共享的工具函数
#
# 使用: source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
# 前提: 调用方定义 PROC_PATTERN (pgrep -f 匹配串) 用于进程管理

# ── Logging ──

log_stage() {
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  $1"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

log_step()  { echo "  [..] $1"; }
log_ok()    { echo "  [OK] $1"; }
log_skip()  { echo "  [OK] $1 (already installed)"; }
log_err()   { echo "  [!!] $1"; }

# ── Hardware detection ──

has_nvidia_gpu() {
    if command -v nvidia-smi >/dev/null 2>&1; then
        nvidia-smi -L >/dev/null 2>&1 && return 0
    fi
    [ -e /dev/nvidia0 ] || [ -e /dev/nvidiactl ]
}

# ── Network checks ──

check_port() {
    local port="$1"
    ss -tlnp 2>/dev/null | grep -q ":${port} " 2>/dev/null
}

check_http() {
    local url="$1"
    env -u LD_LIBRARY_PATH curl -sf -o /dev/null --max-time 2 "$url" 2>/dev/null
}

# ── Process management ──

# Find PIDs matching PROC_PATTERN (or a custom pattern).
# Usage: find_pids [pattern]
find_pids() {
    local pattern="${1:-$PROC_PATTERN}"
    pgrep -f "$pattern" 2>/dev/null | grep -v "^$$\$" || true
}

# Check if the service is running (by process pattern).
# Usage: is_proc_running [pattern]
is_proc_running() {
    local pids
    pids="$(find_pids "${1:-}")"
    [ -n "$pids" ]
}

# Stop a service by killing all processes matching PROC_PATTERN.
# Usage: do_stop [pattern]
do_stop() {
    local pattern="${1:-$PROC_PATTERN}"
    local pids
    pids="$(find_pids "$pattern")"

    if [ -z "$pids" ]; then
        log_err "No running process found for: $pattern"
        return 0
    fi

    echo "$pids" | xargs kill 2>/dev/null || true

    # Wait up to 5s for graceful shutdown
    local i
    for i in 1 2 3 4 5; do
        pids="$(find_pids "$pattern")"
        [ -z "$pids" ] && break
        sleep 1
    done

    # Force kill if still alive
    pids="$(find_pids "$pattern")"
    if [ -n "$pids" ]; then
        echo "$pids" | xargs kill -9 2>/dev/null || true
        sleep 1
    fi

    log_ok "Stopped (pattern: $pattern)"
}

# Save PID to file (for wait_for_ready's process-alive check).
# Usage: save_pid <pid> [pid_file]
save_pid() {
    local pid="$1"
    local pidfile="${2:-${PID_FILE:-}}"
    [ -n "$pidfile" ] && echo "$pid" > "$pidfile"
}

# Wait until a readiness check passes, or timeout.
# Usage: wait_for_ready <check_cmd> <timeout_sec> <name> [interval_sec]
wait_for_ready() {
    local check_cmd="$1"
    local timeout_sec="${2:-30}"
    local name="${3:-service}"
    local interval="${4:-1}"
    local elapsed=0

    while [ "$elapsed" -lt "$timeout_sec" ]; do
        if eval "$check_cmd" >/dev/null 2>&1; then
            return 0
        fi
        # Check if process is still alive
        if ! is_proc_running; then
            log_err "$name process died"
            return 1
        fi
        sleep "$interval"
        elapsed=$((elapsed + interval))
    done
    log_err "Timeout waiting for $name after ${timeout_sec}s"
    return 1
}

# ── Python venv ──

# Create or reuse a Python venv, activate it, install requirements.
# Usage: ensure_venv <venv_dir> <requirements_file>
ensure_venv() {
    local venv_dir="$1"
    local requirements="$2"
    local req_hash=""
    local state_file="$venv_dir/.requirements.sha256"

    if [ ! -f "$venv_dir/bin/activate" ]; then
        log_step "Creating Python venv at $venv_dir ..."
        python3 -m venv --system-site-packages "$venv_dir"
        log_ok "Python venv created"
    else
        log_skip "Python venv"
    fi
    # shellcheck disable=SC1091
    source "$venv_dir/bin/activate"
    # Ensure pip is available (venv on some systems omits it)
    if ! command -v pip &>/dev/null; then
        log_step "Installing pip in venv..."
        python3 -m ensurepip --upgrade 2>/dev/null || python3 -m pip --version || true
    fi

    if command -v sha256sum >/dev/null 2>&1; then
        req_hash="$(sha256sum "$requirements" | awk '{print $1}')"
    else
        req_hash="$(cksum "$requirements" | awk '{print $1":"$2}')"
    fi

    local prebuilt_marker="$venv_dir/.prebuilt"

    # Prebuilt at build time: trust the venv, skip install
    if [ -f "$prebuilt_marker" ] && [ -f "$state_file" ] && [ "$(cat "$state_file")" = "$req_hash" ]; then
        log_skip "Python dependencies (prebuilt)"
        return 0
    fi

    log_step "Installing Python dependencies..."
    local wheels_dir
    wheels_dir="$(dirname "$requirements")/wheels"
    if [ -d "$wheels_dir" ] && [ -n "$(ls -A "$wheels_dir"/*.whl 2>/dev/null)" ]; then
        log_step "Installing from local wheels ($wheels_dir)..."
        python3 -m pip install --no-index --find-links="$wheels_dir" -r "$requirements" --progress-bar on || {
            log_err "Failed to install from local wheels"
            return 1
        }
    else
        log_step "Installing from PyPI..."
        python3 -m pip install --progress-bar on -r "$requirements" || {
            log_err "Failed to install Python dependencies (no network, no wheels)"
            return 1
        }
    fi
    echo "$req_hash" > "$state_file"
}
