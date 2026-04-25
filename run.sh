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

set +u
# shellcheck disable=SC1090
source "$SETUP"
set -u

echo "[INFO] Launching buddy_main..."
exec "$BUDDY_MAIN" "$@"
