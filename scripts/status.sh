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
