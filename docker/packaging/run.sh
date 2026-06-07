#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export LD_LIBRARY_PATH="$DIR/lib/sherpa:$DIR/lib:${LD_LIBRARY_PATH:-}"

# Ensure ROS 2 package prefix environment exists for class_loader/ament_index.
# Prefer full workspace setup when packaged install tree is present; otherwise
# fall back to a minimal prefix so class_loader won't fail on empty AMENT_PREFIX_PATH.
if [[ -f "$DIR/setup.bash" ]]; then
    set +u
    # shellcheck disable=SC1090
    source "$DIR/setup.bash"
    set -u
else
    export AMENT_PREFIX_PATH="${DIR}:${AMENT_PREFIX_PATH:-}"
    export COLCON_PREFIX_PATH="${DIR}:${COLCON_PREFIX_PATH:-}"
    export CMAKE_PREFIX_PATH="${DIR}:${CMAKE_PREFIX_PATH:-}"
fi

if [[ -f "$DIR/etc/buddy.env" ]]; then
    set -a; source "$DIR/etc/buddy.env"; set +a
fi

if [[ -x "$DIR/scripts/status.sh" ]]; then
    "$DIR/scripts/status.sh" --module || true
fi

echo "[INFO] Launching buddy_main..."
cd "$DIR"
exec "$DIR/bin/buddy_main" --base-dir "$DIR"
