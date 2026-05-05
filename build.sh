#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROS2_SETUP="$ROOT_DIR/prebuilt/ros2_core/setup.bash"
OUTPUT_DIR="$ROOT_DIR/output"

usage() {
  echo "Usage: $0 [-c] [colcon args...]"
  echo "  -c    Clean build (remove output/ first)"
  exit 0
}

die() {
  echo "[ERROR] $1" >&2
  [[ -n "${2:-}" ]] && echo "[HINT] $2" >&2
  exit 1
}

check_ros2() {
  [[ -f "$ROS2_SETUP" ]] \
    || die "Missing ROS 2 setup: $ROS2_SETUP" \
           "Extract ros2 tarball first: mkdir -p prebuilt/ros2_core && tar xzf prebuilt/ros2-humble-x86_64.tar.gz -C prebuilt/ros2_core/"
}

check_deps() {
  pkg-config --exists opencv4 2>/dev/null \
    || die "OpenCV development files not found." \
           "sudo apt install libopencv-dev"
}

clean_output() {
  echo "[CLEAN] Removing $OUTPUT_DIR"
  rm -rf "$OUTPUT_DIR"
}

source_ros2() {
  # ROS 2 setup scripts reference uninitialized variables; temporarily relax -u
  set +u
  # shellcheck disable=SC1090
  source "$ROS2_SETUP"
  set -u
}

setup_onnxruntime() {
  local ort_dir="$ROOT_DIR/prebuilt/onnxruntime"
  if [[ ! -d "$ort_dir" ]]; then
    die "Missing ONNX Runtime: $ort_dir" \
        "Extract onnxruntime tarball into prebuilt/ first"
  fi
  export CMAKE_PREFIX_PATH="$ort_dir:${CMAKE_PREFIX_PATH:-}"
  export LD_LIBRARY_PATH="$ort_dir/lib:${LD_LIBRARY_PATH:-}"
}

build() {
  colcon --log-base "$OUTPUT_DIR/log" \
    build --symlink-install \
    --build-base "$OUTPUT_DIR/build" \
    --install-base "$OUTPUT_DIR/install" \
    --cmake-args -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "$@"
}

# --- main ---

CLEAN=false
if [[ "${1:-}" == "-c" ]]; then
  CLEAN=true
  shift
elif [[ "${1:-}" == "-h" ]] || [[ "${1:-}" == "--help" ]]; then
  usage
fi

if [[ "$CLEAN" == true ]]; then
  clean_output
fi

check_ros2
check_deps
source_ros2
setup_onnxruntime

cd "$ROOT_DIR"
build "$@"

echo "[OK] Build finished. Run: source $OUTPUT_DIR/install/setup.bash"
