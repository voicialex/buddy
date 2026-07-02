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
# 策略:
# - root: 直接执行
# - 免密 sudo: 用 sudo -n
# - 需要密码: 每次用 echo pi | sudo -S(不依赖时间戳缓存,在 ssh 非交互场景最可靠)
# SUDO_MODE: "none" / "nopass" / "password"
# SUDO_PASS: 密码(仅 password 模式使用)

SUDO_MODE="none"
SUDO_PASS=""

if [[ $EUID -eq 0 ]]; then
    SUDO_MODE="none"
elif sudo -n true 2>/dev/null; then
    SUDO_MODE="nopass"
elif echo 'pi' | sudo -S true 2>/dev/null; then
    SUDO_MODE="password"
    SUDO_PASS="pi"
fi

_npu_sudo() {
    case "$SUDO_MODE" in
        none)     "$@" ;;
        nopass)   sudo -n "$@" ;;
        password) echo "$SUDO_PASS" | sudo -S "$@" 2>/dev/null ;;
    esac
}

# ── Config ────────────────────────────────────────────────────────────────────

NPU_DEVFREQ="/sys/class/devfreq/fdab0000.npu"
NPU_DEBUGFS="/sys/kernel/debug/rknpu"

# ── Data fetchers ─────────────────────────────────────────────────────────────

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

# 读取 NPU 每核负载。
# 已知格式:
#   "NPU load:  Core0: 0%, Core1: 0%, Core2: 0%,"  (RK3588 主线)
#   "NPU0: 100%, NPU1: 98%, NPU2: 100%"             (旧版本)
# 注意:debugfs 路径普通用户不可见,必须用 sudo 读取
# 用 sudo ls 判断文件存在性(test -f 是 shell builtin,sudo 不执行 builtin)
npu_per_core_load() {
    set +e
    local raw=""
    if _npu_sudo ls "$NPU_DEBUGFS/load" >/dev/null 2>&1; then
        raw="$(_npu_sudo cat "$NPU_DEBUGFS/load" 2>/dev/null)"
    elif _npu_sudo ls "$NPU_DEBUGFS/status" >/dev/null 2>&1; then
        raw="$(_npu_sudo cat "$NPU_DEBUGFS/status" 2>/dev/null)"
    fi
    set -e
    [[ -z "$raw" ]] && return 1

    # 优先匹配 "Core0: 0%" 或 "NPU0: 0%"
    local cores
    cores="$(echo "$raw" | grep -oE '(Core|NPU)[0-9]+: *[0-9]+%' | tr '\n' ' ' | tr -s ' ' | sed 's/^ *//; s/ *$//')"
    if [[ -n "$cores" ]]; then
        echo "$cores"
        return 0
    fi

    # Fallback: 总负载
    if echo "$raw" | grep -qiE 'total.*load|NPU load'; then
        local total
        total="$(echo "$raw" | grep -oE '[0-9]+%' | head -1)"
        [[ -n "$total" ]] && echo "Total: $total"
    fi
}

# 收集加载了 librkllmrt 或 librknnrt 的进程,输出 PID / comm / RSS(KB) / 库名
# /proc/PID/maps 需要 root 读,但 /proc/PID/comm 和 status 普通用户可读
# 用 find -exec 避免 shell glob 展开超长命令行(ARG_MAX)
npu_processes() {
    set +o pipefail
    # find 逐个文件传给 grep,避免 ARG_MAX 溢出
    local maps_files
    maps_files="$(_npu_sudo find /proc -maxdepth 2 -name maps -type f \
        -exec grep -lE 'librknnrt|librkllmrt' {} + 2>/dev/null || true)"
    set -o pipefail
    [[ -z "$maps_files" ]] && return 0

    local seen=""
    while IFS= read -r maps_file; do
        [[ -z "$maps_file" ]] && continue
        local pid
        pid="$(echo "$maps_file" | sed -E 's|^/proc/([0-9]+)/maps|\1|')"
        [[ -z "$pid" ]] && continue
        [[ " $seen " == *" $pid "* ]] && continue
        seen+=" $pid"

        # 判断加载了哪个库(优先 rkllmrt)
        local lib
        if _npu_sudo grep -q librkllmrt "$maps_file" 2>/dev/null; then
            lib="librkllmrt.so"
        elif _npu_sudo grep -q librknnrt "$maps_file" 2>/dev/null; then
            lib="librknnrt.so"
        else
            continue
        fi

        # comm 和 VmRSS 普通用户可读
        local comm rss_kb
        comm="$(cat "/proc/$pid/comm" 2>/dev/null || echo "?")"
        rss_kb="$(awk '/VmRSS:/{print $2}' "/proc/$pid/status" 2>/dev/null || echo 0)"

        printf "%s\t%s\t%s\t%s\n" "$pid" "$comm" "$rss_kb" "$lib"
    done <<< "$maps_files"
}

npu_dmesg_errors() {
    set +o pipefail
    local out
    out="$(_npu_sudo dmesg 2>/dev/null | grep -iE 'rknpu.*fail|rkllm.*error|alloc.*iova.*-12|iommu.*fail' | tail -5 || true)"
    set -o pipefail
    echo "$out"
}

# ── Output ────────────────────────────────────────────────────────────────────
# 所有 render_* 函数把输出 append 到全局变量 FRAME_BUF,避免多次 write 系统调用
# 末尾一次性 printf '%s' "$FRAME_BUF" 输出整帧

FRAME_BUF=""

# 颜色定义(仅在 TTY 输出时启用)
_COLOR_RESET=""
_COLOR_BOLD=""
_COLOR_DIM=""
_COLOR_RED=""
_COLOR_GREEN=""
_COLOR_YELLOW=""
_COLOR_CYAN=""
_COLOR_MAGENTA=""

init_colors() {
    # 仅在交互终端启用颜色,避免管道/日志里的转义码
    # FORCE_COLOR=1 可强制启用(用于调试)
    if [[ "${FORCE_COLOR:-0}" == "1" || (-t 1 && "${TERM:-}" != "dumb") ]]; then
        _COLOR_RESET=$'\033[0m'
        _COLOR_BOLD=$'\033[1m'
        _COLOR_DIM=$'\033[2m'
        _COLOR_RED=$'\033[31m'
        _COLOR_GREEN=$'\033[32m'
        _COLOR_YELLOW=$'\033[33m'
        _COLOR_CYAN=$'\033[36m'
        _COLOR_MAGENTA=$'\033[35m'
    fi
}

render_line() { FRAME_BUF+="$1"$'\n'; }
render_printf() { FRAME_BUF+="$(printf "$@")"$'\n'; }

# 标题栏:统一宽度 56 字符,与下方分隔线对齐
render_header() {
    local title="NPU Monitor"
    local ts
    ts="$(date '+%H:%M:%S')"
    # 总宽度 56,标题居中,时间靠右
    # 格式: ════════ NPU Monitor ═════════════════ 07:22:17 ══
    local width=56
    local title_str=" ${title} "
    local time_str=" ${ts} "
    local fill_total=$((width - ${#title_str} - ${#time_str}))
    local fill_left=$((fill_total / 2))
    local fill_right=$((fill_total - fill_left))
    local bar_l=""
    local bar_r=""
    local i
    for ((i=0; i<fill_left; i++)); do bar_l+="═"; done
    for ((i=0; i<fill_right; i++)); do bar_r+="═"; done
    render_line "${_COLOR_CYAN}${_COLOR_BOLD}══${bar_l}${title_str}${bar_r}${time_str}══${_COLOR_RESET}"
}

render_dashboard() {
    local load freq gov
    load="$(npu_load_pct)"
    freq="$(npu_freq_mhz)"
    gov="$(npu_governor)"

    render_header
    render_line ""

    # Chip/Freq/Gov/Load 行,Load 数值按区间着色
    local load_color="$_COLOR_GREEN"
    if [[ "$load" -ge 90 ]]; then
        load_color="$_COLOR_RED"
    elif [[ "$load" -ge 50 ]]; then
        load_color="$_COLOR_YELLOW"
    fi
    render_printf "  ${_COLOR_DIM}%-10s %10s   %-18s %s${_COLOR_RESET}" "Chip" "Freq" "Gov" "Load"
    render_printf "  ${_COLOR_BOLD}%-10s${_COLOR_RESET} %6s MHz   %-18s ${load_color}%s%%${_COLOR_RESET}" "RK3588" "$freq" "$gov" "$load"
    render_line ""

    # NPU 每核负载(需要 debugfs + root)
    local core_load
    core_load="$(npu_per_core_load || true)"
    if [[ -n "$core_load" ]]; then
        render_line "  ${_COLOR_MAGENTA}NPU Cores${_COLOR_RESET} ${_COLOR_DIM}(debugfs)${_COLOR_RESET}:"
        render_line "    $core_load"
        render_line ""
    fi

    # 进程内存表
    local proc_list total_mb
    proc_list="$(npu_processes)"
    if [[ -n "$proc_list" ]]; then
        render_line "  ${_COLOR_MAGENTA}Memory${_COLOR_RESET} ${_COLOR_DIM}(system RAM used by NPU workloads)${_COLOR_RESET}:"
        render_printf "    ${_COLOR_DIM}%-8s %-18s %8s  %-16s${_COLOR_RESET}" "PID" "Process" "RSS" "Library"
        total_mb=0
        while IFS=$'\t' read -r pid comm rss_kb lib; do
            [[ -z "$pid" ]] && continue
            local rss_mb
            rss_mb=$((rss_kb / 1024))
            total_mb=$((total_mb + rss_mb))
            render_printf "    %-8s %-18s %5d MB  %-16s" "$pid" "$comm" "$rss_mb" "$lib"
        done <<< "$proc_list"
        render_line "    ${_COLOR_DIM}─────────────────────────────────────────────────${_COLOR_RESET}"
        render_printf "    ${_COLOR_BOLD}%-8s %-18s %5d MB${_COLOR_RESET}" "" "Total" "$total_mb"
        render_line ""
    fi
}

render_dmesg() {
    local out
    out="$(npu_dmesg_errors)"
    if [[ -n "$out" ]]; then
        render_line "  ${_COLOR_RED}[!!] dmesg:${_COLOR_RESET}"
        while read -r line; do
            [[ -z "$line" ]] && continue
            render_line "    $line"
        done <<< "$out"
        render_line ""
    fi
}

render_models() {
    local dir="${BUDDY_MODEL_DIR:-}"
    if [[ -z "$dir" ]]; then
        local script_parent
        script_parent="$(cd "$SCRIPT_DIR/.." && pwd)"
        if [[ -d "$script_parent/models/rkllm" ]]; then
            dir="$script_parent/models/rkllm"
        elif [[ -d "/opt/buddy/models/rkllm" ]]; then
            dir="/opt/buddy/models/rkllm"
        fi
    fi
    render_line "  ${_COLOR_MAGENTA}Models${_COLOR_RESET}:"
    if [[ -n "$dir" && -d "$dir" ]]; then
        local found=0
        for f in "$dir"/*.rkllm; do
            [[ -f "$f" ]] || continue
            found=1
            render_printf "    %-55s ${_COLOR_DIM}%s${_COLOR_RESET}" "$(basename "$f")" "$(du -h "$f" | cut -f1)"
        done
        [[ "$found" == "0" ]] && render_line "    ${_COLOR_DIM}(none)${_COLOR_RESET}"
    else
        render_line "    ${_COLOR_DIM}(dir not found; set BUDDY_MODEL_DIR)${_COLOR_RESET}"
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
    # 无闪烁刷新策略:
    # 1. 隐藏光标 \033[?25l,避免光标在输出过程中闪烁
    # 2. 整帧收集到 FRAME_BUF,一次性 printf 输出(单次 write 系统调用)
    # 3. \033[H 光标归位(不清屏),新内容直接覆盖旧内容
    # 4. 末尾 \033[J 清除可能残留的旧行(新帧比旧帧短时)
    # 5. 退出时恢复光标 \033[?25h + 清屏
    [[ -z "${TERM:-}" ]] && export TERM=dumb
    init_colors
    # 隐藏光标,注册退出 trap 恢复(INT/TERM 时退出码保留默认)
    printf '\033[?25l'
    trap 'printf "\033[?25h"; clear 2>/dev/null || true' EXIT
    clear 2>/dev/null || true
    while true; do
        FRAME_BUF=""
        set +e
        render_dashboard
        render_dmesg
        set -e
        # 一次性输出整帧:光标归位 + 帧内容 + 清除屏尾残留
        printf '\033[H%s\033[J' "$FRAME_BUF"
        sleep "$watch_interval"
    done
else
    # 单次模式:收集整帧后一次输出
    init_colors
    set +e
    FRAME_BUF=""
    render_dashboard
    render_models
    render_dmesg
    set -e
    printf '%s\n' "$FRAME_BUF"
fi
