#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKER_DIR="$ROOT_DIR/docker"

usage() {
  cat <<'EOF'
Usage: ./build.sh [-t|--arch x86|arm64] [-d|--device cpu|gpu|npu] [--ros-distro humble|jazzy] [-c] [-p] [-v VERSION] [colcon args...]

Options:
  -t, --arch TARGET   x86 (default) or arm64
  -d, --device DEV    cpu (default), gpu (CUDA), or npu (RKNN)
  --ros-distro DIST   humble (default) or jazzy — ROS 2 distro + base image
  -c          Clean build
  -p, --package       x86 only: package .deb after build (default: skip)
  -v VER      Deb version (default: 1.0.0)

Outputs:
  x86 build:
    output/<distro>/x86_64/install/                       # colcon install
    output/<distro>/x86_64/deb/buddy-robot_<ver>_amd64_<device>.deb (with -p)
  arm64 build:
    output/<distro>/aarch64/deb/buddy-robot_<ver>_arm64_<device>.deb
    output/<distro>/aarch64/deb/buddy-models_<ver>_<device>.tar.gz
    (arm64 always exports runtime deb; models tar builds only when missing)

Examples:
  ./build.sh                        # x86 + cpu + humble incremental
  ./build.sh --ros-distro jazzy     # x86 + cpu + jazzy
  ./build.sh -d gpu                 # x86 + GPU (CUDA ORT)
  ./build.sh -t arm64 -d npu       # arm64 + NPU (RKNN) + humble
  ./build.sh -t arm64 --ros-distro jazzy  # arm64 + jazzy
  ./build.sh -t arm64               # arm64 incremental (~30s if only src changed)
  ./build.sh -t arm64 -c            # arm64 force rebuild (~3min, ccache still helps)
  ./build.sh -t arm64 -v 2.0.0     # arm64 custom version
  ./build.sh --packages-select X    # x86 single package

Optional service packages (LLM, ASR, TTS) — built automatically with arm64:
  output/aarch64/services/buddy-service-llm_<ver>_aarch64.tar.gz
  output/aarch64/services/buddy-service-funasr_<ver>_aarch64.tar.gz
  output/aarch64/services/buddy-service-chattts_<ver>_aarch64.tar.gz

One-command board deploy/run (incremental runtime/models/services):
  ./scripts/deploy_run_arm64_npu.sh --ros-distro ${ROS2_DISTRO:-humble}

Env: BUDDY_PARALLEL_WORKERS=N (default: all cores)

arm64 cache cleanup:
  docker builder prune --filter type=exec.cachemount   # ccache only
  docker builder prune -a -f                           # everything
EOF
  exit 0
}

die() {
  echo "[ERROR] $1" >&2
  [[ -n "${2:-}" ]] && echo "[HINT] $2" >&2
  exit 1
}

# ============================================================
# arm64 build (Docker cross-compile)
# ============================================================
build_arm64() {
  local version="${VERSION:-1.0.0}"
  local parallel="${BUDDY_PARALLEL_WORKERS:-$(nproc)}"
  local output_dir="$ROOT_DIR/output/${ROS2_DISTRO}/aarch64/deb"
  local deb_file="$output_dir/buddy-robot_${version}_arm64_${DEVICE}.deb"
  local models_file="$output_dir/buddy-models_${version}_${DEVICE}.tar.gz"

  echo "[INFO] Building buddy-robot_${version}_arm64_${DEVICE}.deb (cross-compile, ${ROS2_DISTRO})"
  echo "[INFO] Device target: $DEVICE"

  # Ensure prebuilt deps exist (auto-install if missing)
  if [[ ! -d "$ROOT_DIR/prebuilt/aarch64/ros2_core" ]] \
    || [[ ! -d "$ROOT_DIR/prebuilt/aarch64/onnxruntime" ]] \
    || [[ ! -d "$ROOT_DIR/prebuilt/aarch64/sherpa-onnx" ]]; then
    echo "[INFO] Running setup_prebuilt.sh --arch arm64 (distro: ${ROS2_DISTRO}) ..."
    BUDDY_ROS2_DISTRO="$ROS2_DISTRO" "$ROOT_DIR/scripts/setup_prebuilt.sh" --arch arm64
  fi

  # Device-specific prebuilt checks
  if [[ "$DEVICE" == "npu" ]]; then
    if [[ ! -d "$ROOT_DIR/prebuilt/aarch64/rknn/lib" ]]; then
      die "RKNN SDK not found at prebuilt/aarch64/rknn" \
          "Run: cd ../thirdparty && ./build.sh -t arm64 rknn"
    fi
  else
    # Keep Docker COPY stable when non-npu build skips RKNN.
    mkdir -p "$ROOT_DIR/prebuilt/aarch64/rknn"
  fi

  # Ensure runtime third_party prebuilt (runtime only needs OpenCV here).
  if [[ ! -f "$ROOT_DIR/prebuilt/aarch64/opencv/lib/libopencv_core.so" ]]; then
    echo "[INFO] Running build_thirdparty.sh --arch aarch64 ..."
    "$ROOT_DIR/scripts/build_thirdparty.sh" --arch aarch64
  fi

  # Ensure models directory exists
  if [[ ! -d "$ROOT_DIR/models" ]]; then
    echo "[WARN] models/ not found, creating empty placeholder"
    mkdir -p "$ROOT_DIR/models"
  fi
  if [[ "$DEVICE" == "npu" ]]; then
    local missing_zipformer=()
    local zipformer_dir="$ROOT_DIR/models/zipformer-rknn"
    local rf
    for rf in encoder.rknn decoder.rknn joiner.rknn tokens.txt; do
      [[ -f "$zipformer_dir/$rf" ]] || missing_zipformer+=("$rf")
    done
    if [[ ${#missing_zipformer[@]} -gt 0 ]]; then
      die "Missing zipformer-rknn model files: ${missing_zipformer[*]}" \
          "Run: ./scripts/setup_prebuilt.sh --arch arm64 models"
    fi
  fi

  if [[ "$CLEAN" == true ]]; then
    echo "[CLEAN] Removing $output_dir"
    rm -rf "$output_dir"
  fi
  mkdir -p "$output_dir"

  local cache_flag=()
  [[ "$CLEAN" == true ]] && cache_flag=(--no-cache)

  # Build/package app every run.
  DOCKER_BUILDKIT=1 docker build \
    "${cache_flag[@]}" \
    --build-arg TARGET_ARCH=arm64 \
    --build-arg VERSION="$version" \
    --build-arg PARALLEL_WORKERS="$parallel" \
    --build-arg DEVICE="$DEVICE" \
    --target "${ROS2_DISTRO}-export-package" \
    --output "type=local,dest=$output_dir/" \
    -f "$DOCKER_DIR/Dockerfile" \
    "$ROOT_DIR"

  # Build models tar only when missing (manual delete can force refresh).
  if [[ ! -f "$models_file" ]]; then
    echo "[INFO] Models tarball missing, building once: $(basename "$models_file")"
    DOCKER_BUILDKIT=1 docker build \
      "${cache_flag[@]}" \
      --build-arg TARGET_ARCH=arm64 \
      --build-arg VERSION="$version" \
      --build-arg PARALLEL_WORKERS="$parallel" \
      --build-arg DEVICE="$DEVICE" \
      --target "${ROS2_DISTRO}-export-models" \
      --output "type=local,dest=$output_dir/" \
      -f "$DOCKER_DIR/Dockerfile" \
      "$ROOT_DIR"
  else
    echo "[INFO] Reusing existing models tarball: $(basename "$models_file")"
  fi

  echo ""
  [[ -f "$deb_file" ]] && echo "[OK] Package: $deb_file ($(du -sh "$deb_file" | cut -f1))"
  [[ -f "$models_file" ]] && echo "[OK] Models:  $models_file ($(du -sh "$models_file" | cut -f1))"

  # Build optional service packages (LLM, ASR, TTS)
  echo ""
  echo "[INFO] Building service packages..."
  local py_ver="3.10"
  [[ "$ROS2_DISTRO" == "jazzy" ]] && py_ver="3.12"
  "$ROOT_DIR/scripts/package_services.sh" --arch arm64 --version "$version" --python-ver "$py_ver"
  local svc_dir="$ROOT_DIR/output/aarch64/services"
  for svc in "$svc_dir"/*.tar.gz; do
    [[ -f "$svc" ]] && echo "[OK] $(basename "$svc") ($(du -sh "$svc" | cut -f1))"
  done

  if [[ -f "$models_file" ]]; then
    echo ""
    echo "RK3588 deploy (recommended):"
    echo "  ./scripts/deploy_run_arm64_npu.sh --ros-distro ${ROS2_DISTRO}"
    echo "Manual (minimal):"
    echo "  scp $deb_file $models_file user@board:~/"
    echo "  ssh user@board 'cd ~ && rm -rf output/opt/buddy && dpkg -x buddy-robot_${version}_arm64_${DEVICE}.deb output && mkdir -p output/buddy-models-cache && tar xzf buddy-models_${version}_${DEVICE}.tar.gz -C output/buddy-models-cache && ln -sfn ~/output/buddy-models-cache ~/output/opt/buddy/models && ~/output/opt/buddy/run.sh'"
  fi
}

# ============================================================
# x86 build (native)
# ============================================================
build_x86() {
  local arch
  case "$(uname -m)" in
    aarch64|arm64) arch="aarch64" ;;
    *)             arch="x86_64" ;;
  esac

  # Create prebuilt/current symlink
  mkdir -p "$ROOT_DIR/prebuilt"
  ln -sfn "$arch" "$ROOT_DIR/prebuilt/current"

  local ros2_setup="$ROOT_DIR/prebuilt/current/ros2_core/setup.bash"
  local output_dir="$ROOT_DIR/output/${ROS2_DISTRO}/$arch"

  # Ensure prebuilt deps exist (auto-install if missing)
  if [[ ! -f "$ros2_setup" ]]; then
    echo "[INFO] Running setup_prebuilt.sh (distro: ${ROS2_DISTRO}) ..."
    BUDDY_ROS2_DISTRO="$ROS2_DISTRO" "$ROOT_DIR/scripts/setup_prebuilt.sh"
  fi
  [[ -f "$ros2_setup" ]] \
    || die "Missing ROS 2 setup: $ros2_setup" \
           "setup_prebuilt.sh failed — check ros2_core tarball availability"

  # Ensure runtime third_party (OpenCV) prebuilt
  local opencv_dir="$ROOT_DIR/prebuilt/current/opencv"
  if [[ ! -d "$opencv_dir" ]]; then
    echo "[INFO] Running build_thirdparty.sh ..."
    "$ROOT_DIR/scripts/build_thirdparty.sh"
  fi
  [[ -d "$opencv_dir" ]] \
    || die "OpenCV not found at $opencv_dir" \
           "build_thirdparty.sh failed"
  pkg-config --exists alsa 2>/dev/null \
    || die "ALSA not found." "sudo apt install libasound2-dev"
  pkg-config --exists libcurl 2>/dev/null \
    || die "libcurl not found." "sudo apt install libcurl4-openssl-dev"

  local cmake_test_flag=()
  if ! pkg-config --exists gtest 2>/dev/null; then
    echo "[WARN] gtest not found, skipping tests."
    cmake_test_flag=(-DBUILD_TESTING=OFF)
  fi

  # Colcon ignore for venv/services dirs (legacy + new)
  [[ -d "$ROOT_DIR/venv-tts" ]] && touch "$ROOT_DIR/venv-tts/COLCON_IGNORE"
  [[ -d "$ROOT_DIR/services" ]] && touch "$ROOT_DIR/services/COLCON_IGNORE"

  # Clean
  if [[ "$CLEAN" == true ]]; then
    echo "[CLEAN] Removing $output_dir"
    rm -rf "$output_dir"
  fi

  # Source ROS 2
  set +u
  # shellcheck disable=SC1090
  source "$ros2_setup"
  set -u

  # Setup prebuilt libs
  local ort_dir="$ROOT_DIR/prebuilt/current/onnxruntime"
  local sherpa_dir="$ROOT_DIR/prebuilt/current/sherpa-onnx"
  [[ -d "$ort_dir" ]] || die "Missing: $ort_dir"
  [[ -d "$sherpa_dir" ]] || die "Missing: $sherpa_dir"

  # Device-specific checks and cmake flags
  local cmake_device_flags=()
  case "$DEVICE" in
    gpu)
      local ort_gpu_dir="$ROOT_DIR/prebuilt/current/onnxruntime-gpu"
      [[ -d "$ort_gpu_dir" ]] || die "onnxruntime-gpu not found at $ort_gpu_dir" \
          "Download onnxruntime-gpu and extract to prebuilt/$arch/onnxruntime-gpu/"
      ort_dir="$ort_gpu_dir"
      ;;
    npu)
      local rknn_dir="$ROOT_DIR/prebuilt/current/rknn"
      [[ -d "$rknn_dir" ]] || die "RKNN SDK not found at $rknn_dir" \
          "Download RKNN SDK and extract to prebuilt/$arch/rknn/"
      cmake_device_flags+=(-DWITH_RKNN=ON)
      ;;
    cpu) ;;
    *) die "Unknown device: $DEVICE" "Use -d cpu, gpu, or npu" ;;
  esac
  cmake_device_flags+=(-DBUILD_DEVICE="$DEVICE")

  export CMAKE_PREFIX_PATH="$ort_dir:$opencv_dir:${CMAKE_PREFIX_PATH:-}"
  export LD_LIBRARY_PATH="$ort_dir/lib:$sherpa_dir/lib:$opencv_dir/lib:${LD_LIBRARY_PATH:-}"

  # Clean cmake package registry to avoid stale find_package results
  # (e.g. FunASR build pollutes yaml-cpp registry)
  rm -rf ~/.cmake/packages/yaml-cpp 2>/dev/null || true

  cd "$ROOT_DIR"
  colcon --log-base "$output_dir/log" \
    build --symlink-install \
    --build-base "$output_dir/build" \
    --install-base "$output_dir/install" \
    --cmake-args --no-warn-unused-cli -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "${cmake_test_flag[@]}" "${cmake_device_flags[@]}" \
    "$@"

  echo "[OK] Build finished. Run: source $output_dir/install/setup.bash"

  # Package .deb only when explicitly requested
  if [[ "$PACKAGE" == true ]]; then
    local py_ver="3.10"
    [[ "$ROS2_DISTRO" == "jazzy" ]] && py_ver="3.12"
    "$ROOT_DIR/scripts/package_x86.sh" -v "${VERSION:-1.0.0}" -d "$DEVICE" --python-ver "$py_ver"
    echo ""
    echo "[INFO] Building service packages..."
    "$ROOT_DIR/scripts/package_services.sh" --arch x86_64 --version "${VERSION:-1.0.0}" --python-ver "$py_ver"
    local svc_dir="$ROOT_DIR/output/x86_64/services"
    for svc in "$svc_dir"/*.tar.gz; do
      [[ -f "$svc" ]] && echo "[OK] $(basename "$svc") ($(du -sh "$svc" | cut -f1))"
    done
  fi
}

# ============================================================
# Parse args
# ============================================================
TARGET="x86"
DEVICE="cpu"
ROS2_DISTRO="humble"
CLEAN=false
PACKAGE=false
VERSION="1.0.0"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--arch) TARGET="$2"; shift 2 ;;
    -d|--device) DEVICE="$2"; shift 2 ;;
    --ros-distro) ROS2_DISTRO="$2"; shift 2 ;;
    -c) CLEAN=true; shift ;;
    -p|--package) PACKAGE=true; shift ;;
    -v) VERSION="$2"; shift 2 ;;
    -h|--help) usage ;;
    arm64|aarch64) TARGET="arm64"; shift ;;
    x86|x86_64) TARGET="x86"; shift ;;
    humble|jazzy) ROS2_DISTRO="$1"; shift ;;
    *) break ;;
  esac
done

case "$ROS2_DISTRO" in
  humble|jazzy) ;;
  *) die "Unknown ROS 2 distro: $ROS2_DISTRO" "Use --ros-distro humble or jazzy" ;;
esac

case "$TARGET" in
  x86|x86_64)
    build_x86 "$@"
    ;;
  arm64|aarch64)
    if [[ "$CLEAN" == true ]]; then
      echo "[CLEAN] Removing output/${ROS2_DISTRO}/aarch64/"
      rm -rf "$ROOT_DIR/output/${ROS2_DISTRO}/aarch64"
    fi
    build_arm64
    ;;
  *)
    die "Unknown target: $TARGET" "Use -t x86 or -t arm64"
    ;;
esac
