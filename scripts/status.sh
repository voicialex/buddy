#!/usr/bin/env bash
# 查看 buddy 各服务运行状态
#
# Usage: ./scripts/status.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

resolve_rkllm_url() {
    local llm_config="$SCRIPT_DIR/../services/llm/config.yaml"
    local base_url="${BUDDY_RKLLM_URL_BASE:-http://127.0.0.1:8080}"
    local endpoint="${BUDDY_RKLLM_ENDPOINT:-/rkllm_chat}"

    if [[ -f "$llm_config" ]]; then
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
            ' "$llm_config"
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

check_rkllm_ready() {
    local url="$1"
    local code
    code="$(
        curl -sS -o /dev/null -w "%{http_code}" --max-time 8 \
            -H "Content-Type: application/json" \
            -d '{"model":"buddy","messages":[{"role":"user","content":"ping"}],"stream":false}' \
            "$url" 2>/dev/null || true
    )"
    [[ "$code" == "200" || "$code" == "503" ]]
}

# ── Service definitions ──
# Format: name|url|port
SERVICES=(
    "RKLLM|$(resolve_rkllm_url)|8080"
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

    if [[ "$name" == "RKLLM" ]]; then
        if check_port "$port" && check_rkllm_ready "$url"; then
            status="${COL_GREEN}running${COL_RESET}"
            detail="$url ✓"
        elif check_port "$port"; then
            status="${COL_RED}stopped${COL_RESET}"
            detail="$url (port up, api not ready)"
        else
            status="${COL_RED}stopped${COL_RESET}"
            detail="$url"
        fi
    elif check_port "$port"; then
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
