#!/usr/bin/env bash
# 查看 buddy 各服务运行状态
#
# Usage: ./scripts/status.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# ── Service definitions ──
# Format: name|url|port
SERVICES=(
    "Ollama|http://localhost:11434|11434"
    "LLM API|http://127.0.0.1:8002/health|8002"
    "ChatTTS|http://127.0.0.1:9880/docs|9880"
    "FunASR|ws://127.0.0.1:10095|10095"
)

COL_GREEN="\033[32m"
COL_RED="\033[31m"
COL_RESET="\033[0m"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Buddy Services Status"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

printf "  %-12s %-10s %-6s %s\n" "SERVICE" "STATUS" "PORT" "DETAIL"
printf "  %-12s %-10s %-6s %s\n" "-------" "------" "----" "------"

for entry in "${SERVICES[@]}"; do
    IFS='|' read -r name url port <<< "$entry"

    status=""
    detail=""

    if check_port "$port"; then
        status="${COL_GREEN}running${COL_RESET}"
        # Health check for HTTP services
        if [[ "$url" == http* ]]; then
            if check_http "$url"; then
                detail="$url ✓"
            else
                detail="$url (not responding)"
            fi
        else
            detail="$url ✓"
        fi
    else
        status="${COL_RED}stopped${COL_RESET}"
    fi

    printf "  %-12s %-22b %-6s %s\n" "$name" "$status" "$port" "$detail"
done

# Extra info: Ollama model status
if check_http "http://localhost:11434"; then
    echo ""
    echo "  Ollama models loaded:"
    ollama ps 2>/dev/null | sed 's/^/    /' || echo "    (none)"
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Buddy App (C++ ROS 2)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Check buddy_main process
BUDDY_PIDS=$(pgrep -f buddy_main 2>/dev/null || true)
if [ -n "$BUDDY_PIDS" ]; then
    printf "  %-12s %b\n" "Process" "${COL_GREEN}running${COL_RESET}  (PID: $(echo "$BUDDY_PIDS" | tr '\n' ' '))"
    if command -v ps >/dev/null 2>&1; then
        for pid in $BUDDY_PIDS; do
            elapsed=$(ps -o etime= -p "$pid" 2>/dev/null | tr -d ' ')
            if [ -n "$elapsed" ]; then
                printf "    PID %-6s uptime: %s\n" "$pid" "$elapsed"
            fi
        done
    fi
else
    printf "  %-12s %b\n" "Process" "${COL_RED}stopped${COL_RESET}"
fi

# Check systemd service
if command -v systemctl >/dev/null 2>&1; then
    svc_status=$(systemctl is-active buddy 2>/dev/null || true)
    case "$svc_status" in
        active)   svc_color="${COL_GREEN}active${COL_RESET}" ;;
        inactive) svc_color="${COL_RED}inactive${COL_RESET}" ;;
        "")       svc_color="${COL_RED}not installed${COL_RESET}" ;;
        *)        svc_color="${COL_RED}${svc_status}${COL_RESET}" ;;
    esac
    printf "  %-12s %b\n" "systemd" "$svc_color"
fi

# Check ROS 2 nodes
if command -v ros2 >/dev/null 2>&1; then
    nodes=$(ros2 node list 2>/dev/null || true)
    if [ -n "$nodes" ]; then
        echo ""
        echo "  ROS 2 nodes:"
        echo "$nodes" | sed 's/^/    /'
    fi
fi

echo ""
