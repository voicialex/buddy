#!/usr/bin/env bash
# NPU 实时监控脚本
#
# Usage:
#   ./scripts/npu_monitor.sh            # 单次完整输出
#   ./scripts/npu_monitor.sh -w 2       # 每 2 秒刷新（精简模式）
#   ./scripts/npu_monitor.sh -w         # 每 1 秒刷新
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[[ -f "$SCRIPT_DIR/common.sh" ]] && source "$SCRIPT_DIR/common.sh"

# ── sudo wrapper ──────────────────────────────────────────────────────────────

SUDO=""
if [[ $EUID -eq 0 ]]; then
    SUDO=""
elif sudo -n true 2>/dev/null; then
    SUDO="sudo -n"
elif echo 'pi' | sudo -S true 2>/dev/null; then
    SUDO="echo pi | sudo -S"
fi

_npu_sudo() {
    if [[ "$SUDO" == *"echo pi"* ]]; then
        eval "$SUDO $*"
    elif [[ -n "$SUDO" ]]; then
        $SUDO "$@"
    else
        "$@"
    fi
}

# ── Config ────────────────────────────────────────────────────────────────────

NPU_DEVFREQ="/sys/class/devfreq/fdab0000.npu"
IOVA_TOTAL_MB=4096

# ── Data fetchers ─────────────────────────────────────────────────────────────

npu_users_raw() {
    set +o pipefail
    _npu_sudo grep -l librknnrt /proc/*/maps 2>/dev/null || true
    set -o pipefail
}

npu_total_mapped() {
    local total=0
    set +o pipefail
    total="$(_npu_sudo grep librknnrt /proc/*/maps 2>/dev/null | awk '{sum+=$2} END{printf "%.0f", sum/1024}' || echo 0)"
    set -o pipefail
    echo "${total:-0}"
}

npu_load_pct() {
    # /sys/class/devfreq/.../load returns "N@FreqHz", extract N
    local raw
    raw="$(cat "$NPU_DEVFREQ/load" 2>/dev/null || echo "0@0")"
    echo "${raw%%@*}"
}

npu_freq_mhz() {
    local hz
    hz="$(cat "$NPU_DEVFREQ/cur_freq" 2>/dev/null || echo 0)"
    echo "$((hz / 1000000))"
}

npu_governor() {
    cat "$NPU_DEVFREQ/governor" 2>/dev/null || echo "N/A"
}

npu_dmesg_errors() {
    set +o pipefail
    _npu_sudo dmesg 2>/dev/null | grep -iE 'rknpu.*fail|rkllm.*error|alloc.*iova.*-12' | tail -5 || true
    set -o pipefail
}

# ── Visual helpers ────────────────────────────────────────────────────────────

bar() {
    # $1 = current MB, $2 = total MB, $3 = width in chars
    local cur="$1" total="$2" width="${3:-30}"
    local pct filled empty
    pct="$(awk "BEGIN {printf \"%.0f\", ($cur/$total)*100}")"
    filled="$(awk "BEGIN {printf \"%.0f\", ($cur/$total)*$width}")"
    empty=$((width - filled))
    printf "["
    printf "%${filled}s" '' | tr ' ' '='
    printf "%${empty}s" '' | tr ' ' '-'
    printf "] %3s%%  %s/%s MB" "$pct" "$cur" "$total"
}

# ── Output ────────────────────────────────────────────────────────────────────

print_dashboard() {
    local load freq gov total users
    load="$(npu_load_pct)"
    freq="$(npu_freq_mhz)"
    gov="$(npu_governor)"
    total="$(npu_total_mapped)"

    echo "====== NPU Monitor ====== $(date '+%H:%M:%S')"
    echo ""
    printf "  %-6s  %8s  %-14s  %s\n" "Chip" "Freq" "Gov" "Load"
    printf "  %-6s  %6s MHz  %-14s  %s%%\n" "RK3588" "$freq" "$gov" "$load"
    echo ""
    printf "  IOVA  %s\n" "$(bar "$total" "$IOVA_TOTAL_MB" 36)"

    users="$(npu_users_raw)"
    if [[ -n "$users" ]]; then
        echo ""
        printf "  %-6s  %-16s  %7s  %s\n" "PID" "Process" "NPU(MB)" "Uptime"
        while IFS= read -r f <&3; do
            [[ -z "$f" ]] && continue
            local pid comm rss_kb rss_mb uptime_str
            pid="$(echo "$f" | cut -d/ -f3)"
            comm="$(cat "/proc/$pid/comm" 2>/dev/null || echo "?")"
            rss_kb="$({ set +o pipefail; _npu_sudo awk '/librknnrt/{sum+=$2} END{print int(sum)}' "$f" 2>/dev/null || echo 0; })"
            rss_mb=$((rss_kb / 1024))
            uptime_str="$(awk '{t=int($1/100)} END{printf "%dm%02ds", t/60, t%60}' "/proc/$pid/stat" 2>/dev/null || echo "?")"
            printf "  %-6s  %-16s  %5d MB  %s\n" "$pid" "$comm" "$rss_mb" "$uptime_str"
        done 3<<< "$users"
    fi
}

print_dmesg() {
    local out
    out="$(npu_dmesg_errors)"
    if [[ -n "$out" ]]; then
        echo ""
        echo "  [!!] dmesg:"
        while read -r line; do
            [[ -z "$line" ]] && continue
            echo "    $line"
        done <<< "$out"
    fi
}

print_models() {
    local dir="${BUDDY_MODEL_DIR:-/opt/buddy/models/rkllm}"
    echo ""
    echo "  Models:"
    if [[ -d "$dir" ]]; then
        for f in "$dir"/*.rkllm; do
            [[ -f "$f" ]] || { echo "    (none)"; break; }
            printf "    %-55s %s\n" "$(basename "$f")" "$(du -h "$f" | cut -f1)"
        done
    fi
}

# ── Main ──────────────────────────────────────────────────────────────────────

watch_interval=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -w|--watch)
            watch_interval="${2:-1}"
            [[ "$watch_interval" =~ ^[0-9]+$ ]] || watch_interval=1
            shift 2 2>/dev/null || shift
            ;;
        -h|--help)
            echo "Usage: $0 [-w SECONDS]"
            echo ""
            echo "  无参数      单次完整输出（含 dmesg / 模型列表）"
            echo "  -w N        每 N 秒刷新（精简面板模式，Ctrl+C 退出）"
            echo "  -w          每 1 秒刷新"
            exit 0
            ;;
        *) shift ;;
    esac
done

if [[ "$watch_interval" -gt 0 ]]; then
    while true; do
        clear
        print_dashboard
        print_dmesg
        sleep "$watch_interval"
    done
else
    print_dashboard
    print_models
    print_dmesg
    echo ""
fi
