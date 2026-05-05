#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="$ROOT_DIR/output/install"
SETUP="$INSTALL_DIR/setup.bash"

if [[ ! -f "$SETUP" ]]; then
  echo "[ERROR] Workspace not built yet: $SETUP not found" >&2
  echo "[HINT]  Run ./build.sh first" >&2
  exit 1
fi

BUDDY_MAIN="$INSTALL_DIR/buddy_app/lib/buddy_app/buddy_main"
if [[ ! -x "$BUDDY_MAIN" ]]; then
  echo "[ERROR] buddy_main not found: $BUDDY_MAIN" >&2
  exit 1
fi

# --- Check for existing buddy_main process ---
RUNNING_PIDS=$(pgrep -f "$BUDDY_MAIN" 2>/dev/null || true)
if [[ -n "$RUNNING_PIDS" ]]; then
  echo "[WARN] buddy_main is already running (PID: $(echo "$RUNNING_PIDS" | tr '\n' ' '))" >&2
  read -rp "[WARN] Kill existing process and restart? [y/N] " answer
  case "$answer" in
    [yY]|[yY][eE][sS])
      echo "[INFO] Killing PID(s): $RUNNING_PIDS"
      echo "$RUNNING_PIDS" | xargs kill
      # Wait briefly for graceful shutdown
      for i in 1 2 3 4 5; do
        RUNNING_PIDS=$(pgrep -f "$BUDDY_MAIN" 2>/dev/null || true)
        [[ -z "$RUNNING_PIDS" ]] && break
        sleep 1
      done
      # Force kill if still alive
      RUNNING_PIDS=$(pgrep -f "$BUDDY_MAIN" 2>/dev/null || true)
      if [[ -n "$RUNNING_PIDS" ]]; then
        echo "[WARN] Process did not exit gracefully, force killing..." >&2
        echo "$RUNNING_PIDS" | xargs kill -9
        sleep 1
      fi
      echo "[INFO] Existing process terminated."
      ;;
    *)
      echo "[INFO] Aborted." >&2
      exit 0
      ;;
  esac
fi

set +u
# shellcheck disable=SC1090
source "$SETUP"
set -u

echo "[INFO] Launching buddy_main..."
exec "$BUDDY_MAIN" "$@"
