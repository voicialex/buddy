#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROS2_SETUP="$ROOT_DIR/prebuilt/setup.bash"

if [[ ! -f "$ROS2_SETUP" ]]; then
  echo "[ERROR] Missing ROS 2 setup: $ROS2_SETUP" >&2
  echo "[HINT] Extract ros2 tarball into prebuilt/ first:" >&2
  echo "       tar xzf ros2-humble-x86_64.tar.gz -C prebuilt/" >&2
  exit 1
fi

# ROS 2 setup scripts reference uninitialized variables; temporarily relax -u
set +u
# shellcheck disable=SC1090
source "$ROS2_SETUP"
set -u

cd "$ROOT_DIR"
OUTPUT_DIR="$ROOT_DIR/output"
colcon --log-base "$OUTPUT_DIR/log" \
  build --symlink-install \
  --build-base "$OUTPUT_DIR/build" \
  --install-base "$OUTPUT_DIR/install" \
  "$@"

echo "[OK] Build finished. Run: source $OUTPUT_DIR/install/setup.bash"
