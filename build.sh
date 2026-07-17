#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKER_DIR="$ROOT_DIR/docker"

usage() {
  cat <<'EOF'
Usage: ./build.sh [-t x86|arm64] [-d cpu|gpu|npu] [--ros-distro jazzy|humble] [-c] [-j N] [-p] [-v VER] [--all] [colcon args...]

Options:
  -t, --arch TARGET   auto (default, matches host arch)
  -d, --device DEV    cpu (default), gpu (CUDA), or npu (RKNN)
  --ros-distro DIST   auto (default, matches host: 22.04→humble, 24.04→jazzy)
  -c          Clean build
  --native    Force native build (skip Docker fallback)
  -j N        Parallel workers (default: $(nproc))
  -p, --package       Package .deb after native build
  -v VER      Deb version (default: 1.0.0)
  --all               Parallel build all 4 combos (arm64/x86 × humble/jazzy)
  --rebuild-base      Force rebuild Docker base image

Build runs natively when host arch == target arch. ROS distro auto-detected from
host OS (22.04→humble, 24.04→jazzy). Mismatched distro falls back to Docker.
Use --native to force native build and skip the Docker fallback.

Outputs:
  Native:  output/<distro>/<arch>/install/
  Docker:  output/<distro>/<arch>/deb/buddy-robot_<ver>_<deb_arch>_<device>.deb

Examples:
  ./build.sh                    # auto arch + auto distro → native
  ./build.sh jazzy              # force jazzy (Docker if host < 24.04)
  ./build.sh -t arm64           # native on arm64 board, Docker on x86 PC
  ./build.sh -t arm64 -d npu    # arm64 + NPU (RK3588)
  ./build.sh -t arm64 -c        # arm64 clean build
  ./build.sh -d gpu             # x86 + CUDA
  ./build.sh -p                 # native build + package .deb
  ./build.sh --all              # all 4 arch × distro combos in parallel

Deploy:
  ./scripts/deploy_run_arm64_npu.sh --ros-distro jazzy
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
build_docker() {
  local docker_arch="$1"   # arm64 | x86_64
  local prebuilt_arch="$2" # aarch64 | x86_64
  local deb_arch="$3"      # arm64 | amd64
  local version="${VERSION:-1.0.0}"
  local parallel="${PARALLEL}"
  local output_dir="$ROOT_DIR/output/${ROS2_DISTRO}/${prebuilt_arch}/deb"
  local deb_file="$output_dir/buddy-robot_${version}_${deb_arch}_${DEVICE}.deb"
  local models_file="$output_dir/buddy-models_${version}_${DEVICE}.tar.gz"

  echo "[INFO] Building buddy-robot_${version}_${deb_arch}_${DEVICE}.deb (docker, ${ROS2_DISTRO})"
  echo "[INFO] Device target: $DEVICE"

  # Ensure prebuilt deps exist (auto-install if missing)
  if [[ ! -d "$ROOT_DIR/prebuilt/${prebuilt_arch}/ros2_core/$ROS2_DISTRO" ]] \
    || [[ ! -d "$ROOT_DIR/prebuilt/${prebuilt_arch}/onnxruntime" ]] \
    || [[ ! -d "$ROOT_DIR/prebuilt/${prebuilt_arch}/sherpa-onnx" ]]; then
    echo "[INFO] Running setup_prebuilt.sh --arch ${prebuilt_arch} (distro: ${ROS2_DISTRO}) ..."
    BUDDY_ROS2_DISTRO="$ROS2_DISTRO" "$ROOT_DIR/scripts/setup_prebuilt.sh" --arch "${prebuilt_arch}"
  fi

  # Device-specific prebuilt checks
  if [[ "$DEVICE" == "npu" ]]; then
    if [[ ! -d "$ROOT_DIR/prebuilt/${prebuilt_arch}/rknn/lib" ]]; then
      die "RKNN SDK not found at prebuilt/${prebuilt_arch}/rknn" \
          "Run: cd ../thirdparty && ./build.sh -t arm64 rknn"
    fi
  else
    # Keep Docker COPY stable when non-npu build skips RKNN.
    mkdir -p "$ROOT_DIR/prebuilt/${prebuilt_arch}/rknn"
  fi

  # Ensure runtime third_party prebuilt (runtime only needs OpenCV here).
  if [[ ! -f "$ROOT_DIR/prebuilt/${prebuilt_arch}/opencv/lib/libopencv_core.so" ]]; then
    echo "[INFO] Running build_thirdparty.sh --arch ${prebuilt_arch} ..."
    "$ROOT_DIR/scripts/build_thirdparty.sh" --arch "${prebuilt_arch}"
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
          "Run: ./scripts/setup_prebuilt.sh --arch ${prebuilt_arch} models"
    fi
  fi

  if [[ "$CLEAN" == true ]]; then
    echo "[CLEAN] Removing $output_dir"
    rm -rf "$output_dir"
  fi
  mkdir -p "$output_dir"

  # 预构建 base image（apt + pip deps），避免每次 build 重跑 apt update
  # 重建: ./build.sh --rebuild-base
  local base_image_tag="buddy-base:${ROS2_DISTRO}-${docker_arch}"
  local base_cache_flag=()
  [[ "$CLEAN" == true ]] && base_cache_flag=(--no-cache)

  if ! docker image inspect "$base_image_tag" >/dev/null 2>&1 || [[ "$REBUILD_BASE" == true ]]; then
    echo "[INFO] Building base image: $base_image_tag"
    DOCKER_BUILDKIT=1 docker build \
      "${base_cache_flag[@]}" \
      --target "${ROS2_DISTRO}-base" \
      -t "$base_image_tag" \
      --build-arg TARGET_ARCH="$docker_arch" \
      --build-arg DEVICE="$DEVICE" \
      -f "$DOCKER_DIR/Dockerfile.base" \
      "$ROOT_DIR"
  else
    echo "[INFO] Reusing base image: $base_image_tag"
  fi

  local cache_flag=()
  [[ "$CLEAN" == true ]] && cache_flag=(--no-cache)

  # Build/package app every run.
  DOCKER_BUILDKIT=1 docker build \
    "${cache_flag[@]}" \
    --build-arg TARGET_ARCH="$docker_arch" \
    --build-arg VERSION="$version" \
    --build-arg PARALLEL_WORKERS="$parallel" \
    --build-arg DEVICE="$DEVICE" \
    --build-arg HUMBLE_BASE_IMAGE="$base_image_tag" \
    --build-arg JAZZY_BASE_IMAGE="$base_image_tag" \
    --target "${ROS2_DISTRO}-export-package" \
    --output "type=local,dest=$output_dir/" \
    -f "$DOCKER_DIR/Dockerfile" \
    "$ROOT_DIR"

  # Build models tar only when missing (manual delete can force refresh).
  if [[ ! -f "$models_file" ]]; then
    echo "[INFO] Models tarball missing, building once: $(basename "$models_file")"
    DOCKER_BUILDKIT=1 docker build \
      "${cache_flag[@]}" \
      --build-arg TARGET_ARCH="$docker_arch" \
      --build-arg VERSION="$version" \
      --build-arg PARALLEL_WORKERS="$parallel" \
      --build-arg DEVICE="$DEVICE" \
      --build-arg HUMBLE_BASE_IMAGE="$base_image_tag" \
      --build-arg JAZZY_BASE_IMAGE="$base_image_tag" \
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
  "$ROOT_DIR/scripts/package_services.sh" --arch "${prebuilt_arch}" --version "$version" --python-ver "$py_ver" --ros-distro "$ROS2_DISTRO"
  local svc_dir="$ROOT_DIR/output/${ROS2_DISTRO}/${prebuilt_arch}/services"
  for svc in "$svc_dir"/*.tar.gz; do
    [[ -f "$svc" ]] && echo "[OK] $(basename "$svc") ($(du -sh "$svc" | cut -f1))"
  done

  if [[ "$deb_arch" == "arm64" ]]; then
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  Deploy"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "RK3588 deploy (recommended — one command):"
    echo "  ./scripts/deploy_run_arm64_npu.sh --ros-distro ${ROS2_DISTRO}"
    echo ""
    echo "Manual deploy (runtime first, then models):"
    echo "  # 1. Deploy runtime"
    echo "  scp $deb_file work:~/"
    echo "  ssh work 'cd ~ && dpkg -x buddy-robot_${version}_arm64_${DEVICE}.deb output'"
    if [[ -f "$models_file" ]]; then
      echo "  # 2. Deploy models directly into buddy/models"
      echo "  scp $models_file work:~/"
      echo "  ssh work 'cd ~ && tar xzf buddy-models_${version}_${DEVICE}.tar.gz -C output/opt/buddy/models'"
    fi
    echo "  # 3. Switch engines to NPU"
    echo "  ssh work 'cd ~/output/opt/buddy && ./scripts/switch_runtime.sh npu'"
    echo "  # 4. Run"
    echo "  ssh work '~/output/opt/buddy/run.sh'"
  fi
}

# ============================================================
# Native build — auto-detects arch, routes to Docker only when host OS is too old
# ============================================================
build_native() {
  # Jazzy needs GLIBC 2.38 (Ubuntu 24.04+). If the host is too old, fall back
  # to Docker instead of failing at link time. --native skips this check.
  if [[ "$ROS2_DISTRO" == "jazzy" && "$FORCE_NATIVE" != true ]]; then
    local need_docker=true
    # Method 1: ldd --version (works everywhere)
    if ldd --version 2>&1 | grep -oP 'GLIBC \K[0-9]+\.[0-9]+' | head -1 \
      | { read ver; [[ -n "$ver" ]] && printf '%s\n' 2.38 "$ver" | sort -VC 2>/dev/null; }; then
      need_docker=false
    # Method 2: OS version fallback (minimal systems, empty ldconfig cache)
    elif grep -q 'VERSION_ID="2[4-9]\.' /etc/os-release 2>/dev/null; then
      need_docker=false
    fi
    if $need_docker; then
      local docker_arch prebuilt_arch deb_arch
      case "$(uname -m)" in
        aarch64|arm64) docker_arch="arm64"; prebuilt_arch="aarch64"; deb_arch="arm64" ;;
        *)             docker_arch="x86_64"; prebuilt_arch="x86_64"; deb_arch="amd64" ;;
      esac
      echo "[INFO] Host too old for Jazzy — routing through Docker (use --native to force)."
      build_docker "$docker_arch" "$prebuilt_arch" "$deb_arch"
      return $?
    fi
  fi

  local arch
  case "$(uname -m)" in
    aarch64|arm64) arch="aarch64" ;;
    *)             arch="x86_64" ;;
  esac

  # Create prebuilt/current symlink
  mkdir -p "$ROOT_DIR/prebuilt"
  ln -sfn "$arch" "$ROOT_DIR/prebuilt/current"

  local ros2_setup="$ROOT_DIR/prebuilt/current/ros2_core/${ROS2_DISTRO}/setup.bash"
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

  # Colcon ignore for services dir (skip Python services in ROS 2 build)
  [[ -d "$ROOT_DIR/services" ]] && touch "$ROOT_DIR/services/COLCON_IGNORE"

  # Clean
  if [[ "$CLEAN" == true ]]; then
    echo "[CLEAN] Removing $output_dir"
    rm -rf "$output_dir"
  fi

  # Isolate from any system ROS installation
  unset AMENT_PREFIX_PATH CMAKE_PREFIX_PATH ROS_DISTRO COLCON_PREFIX_PATH 2>/dev/null || true

  # Source ROS 2 from prebuilt
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

  # Auto-clear stale cmake cache that references system ROS (e.g. /opt/ros/)
  if grep -rq "/opt/ros/" "$output_dir/build" 2>/dev/null; then
    echo "[WARN] Stale cmake cache from system ROS detected, cleaning build dir."
    echo "[HINT] System ROS env leaked into previous build. Use --native to isolate."
    rm -rf "$output_dir/build"
  fi

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
    "$ROOT_DIR/scripts/package_x86.sh" -v "${VERSION:-1.0.0}" -d "$DEVICE" --ros-distro "$ROS2_DISTRO" --python-ver "$py_ver"
    echo ""
    echo "[INFO] Building service packages..."
    "$ROOT_DIR/scripts/package_services.sh" --arch "$arch" --version "${VERSION:-1.0.0}" --python-ver "$py_ver" --ros-distro "$ROS2_DISTRO"
    local svc_dir="$ROOT_DIR/output/${ROS2_DISTRO}/${arch}/services"
    for svc in "$svc_dir"/*.tar.gz; do
      [[ -f "$svc" ]] && echo "[OK] $(basename "$svc") ($(du -sh "$svc" | cut -f1))"
    done
  fi
}

# ============================================================
# Parallel build all 4 combos (arm64/x86 × humble/jazzy)
# ============================================================
prepare_all_prebuilt() {
  echo "━━━ Step 1: Prepare prebuilt deps (serial) ━━━"

  # 4 (arch, distro) combos — setup_prebuilt.sh is "skip if exists", but
  # pre-checking avoids even invoking it when deps are already in place.
  local combos=(
    "aarch64|humble"
    "aarch64|jazzy"
    "x86_64|humble"
    "x86_64|jazzy"
  )
  for combo in "${combos[@]}"; do
    local arch distro
    IFS='|' read -r arch distro <<< "$combo"
    local ros2_setup="$ROOT_DIR/prebuilt/$arch/ros2_core/$distro/setup.bash"
    if [[ -f "$ros2_setup" ]] \
       && [[ -d "$ROOT_DIR/prebuilt/$arch/onnxruntime" ]] \
       && [[ -d "$ROOT_DIR/prebuilt/$arch/sherpa-onnx" ]]; then
      echo "  [SKIP] $arch/$distro (prebuilt ready)"
      continue
    fi
    echo "  [..] Preparing $arch/$distro ..."
    BUDDY_ROS2_DISTRO="$distro" "$ROOT_DIR/scripts/setup_prebuilt.sh" --arch "$arch"
  done

  # Third-party (OpenCV/libcurl) per arch — skip if already staged.
  for arch in aarch64 x86_64; do
    if [[ -f "$ROOT_DIR/prebuilt/$arch/opencv/lib/libopencv_core.so" ]]; then
      echo "  [SKIP] thirdparty/$arch (already built)"
      continue
    fi
    echo "  [..] Building thirdparty for $arch ..."
    "$ROOT_DIR/scripts/build_thirdparty.sh" --arch "$arch"
  done

  echo ""
}

fmt_duration() {
  local elapsed=$1
  if [[ $elapsed -lt 60 ]]; then
    printf '%ds' "$elapsed"
  elif [[ $elapsed -lt 3600 ]]; then
    printf '%dm%ds' $((elapsed / 60)) $((elapsed % 60))
  else
    printf '%dh%dm' $((elapsed / 3600)) $(((elapsed % 3600) / 60))
  fi
}

build_all() {
  local version="${VERSION:-1.0.0}"
  local log_dir="$ROOT_DIR/output/.build-all-logs"
  mkdir -p "$log_dir"
  rm -f "$log_dir"/*.log 2>/dev/null || true

  # Step 1: serial prebuilt prep — guarantees all deps ready before parallel
  # builds start, so build_docker/build_x86's internal checks all skip.
  prepare_all_prebuilt

  echo "━━━ Step 2: Parallel build ×4 (device=$DEVICE, ver=$version) ━━━"
  echo "  Logs: $log_dir/<label>.log   [Ctrl-C to abort]"
  echo ""

  local combos=(
    "arm64-humble|arm64|humble"
    "arm64-jazzy|arm64|jazzy"
    "x86-humble|x86|humble"
    "x86-jazzy|x86|jazzy"
  )

  local pids=() labels=() logs=() statuses=() start_times=() end_times=()

  for combo in "${combos[@]}"; do
    local label target distro
    IFS='|' read -r label target distro <<< "$combo"
    local log="$log_dir/${label}.log"

    (
      export ROS2_DISTRO="$distro"
      if [[ "$target" == "arm64" ]]; then
        build_docker arm64 aarch64 arm64 > "$log" 2>&1
      else
        build_native > "$log" 2>&1
      fi
    ) &
    pids+=($!)
    labels+=("$label")
    logs+=("$log")
    statuses+=("running")
    start_times+=("$(date +%s)")
    end_times+=("")
  done

  # Terminal UI: hide cursor, restore on exit
  if [[ -t 1 ]]; then
    tput civis 2>/dev/null || true
    trap 'tput cnorm 2>/dev/null || true; kill $(jobs -p) 2>/dev/null; echo "[ABORT]"; exit 130' INT
    trap 'tput cnorm 2>/dev/null || true' EXIT
  fi

  local cols
  cols=$(tput cols 2>/dev/null || echo 80)
  local log_lines=$(( (cols - 8) / 2 ))   # rough width budget per side
  [[ $log_lines -gt 120 ]] && log_lines=120
  [[ $log_lines -lt 40 ]] && log_lines=40

  while true; do
    # Detect finished builds (non-blocking)
    local all_done=true
    for i in "${!pids[@]}"; do
      if [[ "${statuses[$i]}" == "running" ]] && ! kill -0 "${pids[$i]}" 2>/dev/null; then
        if wait "${pids[$i]}" 2>/dev/null; then
          statuses[$i]="ok"
        else
          statuses[$i]="fail"
        fi
        end_times[$i]="$(date +%s)"
      fi
      [[ "${statuses[$i]}" == "running" ]] && all_done=false
    done

    # Move cursor to top, redraw
    tput cup 0 0 2>/dev/null || clear
    printf '━━━ Parallel build ×4 (device=%s, ver=%s) ━━━' "$DEVICE" "$version"
    tput el 2>/dev/null || true; echo ""
    printf '  Logs: %s/<label>.log   [Ctrl-C to abort]' "$log_dir"
    tput el 2>/dev/null || true; echo ""
    tput el 2>/dev/null || true; echo ""

    for i in "${!labels[@]}"; do
      local icon color
      case "${statuses[$i]}" in
        running) icon="[..]"; color=$'\033[33m' ;;
        ok)      icon="[OK]"; color=$'\033[32m' ;;
        fail)    icon="[X]";  color=$'\033[31m' ;;
      esac
      local reset=$'\033[0m'

      # Elapsed time: live while running, frozen at completion
      local now elapsed time_str
      now=$(date +%s)
      if [[ -z "${end_times[$i]}" ]]; then
        elapsed=$(( now - ${start_times[$i]} ))
      else
        elapsed=$(( ${end_times[$i]} - ${start_times[$i]} ))
      fi
      time_str=$(fmt_duration "$elapsed")

      # Header line: "── [..] label 1m23s ────..." padded to terminal width
      # visible: "── " (3) + icon + " " (1) + label + " " (1) + time_str + " " (1)
      local visible_len=$(( 6 + ${#icon} + ${#labels[$i]} + ${#time_str} ))
      local pad=$(( cols - visible_len ))
      [[ $pad -lt 4 ]] && pad=4
      local dashes
      dashes=$(printf '─%.0s' $(seq 1 "$pad"))
      printf '── %s%s%s %s %s %s' "$color" "$icon" "$reset" "${labels[$i]}" "$time_str" "$dashes"
      tput el 2>/dev/null || true
      echo ""

      # Last 8 lines of log — convert \r to \n first (docker progress lines)
      local lines=()
      if [[ -f "${logs[$i]}" ]]; then
        while IFS= read -r line; do
          lines+=("$line")
        done < <(tr '\r' '\n' < "${logs[$i]}" 2>/dev/null | tail -8)
      fi
      while [[ ${#lines[@]} -lt 8 ]]; do lines+=(""); done

      local max_len=$((cols - 4))
      for line in "${lines[@]}"; do
        # Strip ANSI color codes
        line="${line//$'\033('[0-9;]*m/}"
        if [[ ${#line} -gt $max_len ]]; then
          line="${line:0:$((max_len-3))}..."
        fi
        printf '  %s' "$line"
        tput el 2>/dev/null || true
        echo ""
      done

      # Blank line between blocks (not after the last one)
      if [[ $i -lt $((${#labels[@]} - 1)) ]]; then
        echo ""
      fi
    done

    tput ed 2>/dev/null || true   # clear to end of screen

    if $all_done; then
      break
    fi
    sleep 1
  done

  # Summary
  echo ""
  local failed=()
  for i in "${!labels[@]}"; do
    [[ "${statuses[$i]}" == "fail" ]] && failed+=("${labels[$i]}")
  done

  if [[ ${#failed[@]} -gt 0 ]]; then
    echo "[FAIL] ${#failed[@]}/4 builds failed: ${failed[*]}"
    echo "[HINT] tail -50 $log_dir/${failed[0]}.log"
    return 1
  fi
  echo "[OK] All 4 builds passed"
}

# ============================================================
# Parse args
# ============================================================
detect_ros_distro() {
  local ver
  ver=$(grep -oP 'VERSION_ID="\K[0-9]+\.[0-9]+' /etc/os-release 2>/dev/null || echo "")
  case "$ver" in
    24.*) echo "jazzy" ;;
    22.*) echo "humble" ;;
    *)    echo "humble" ;;   # safe default
  esac
}

TARGET="${BUDDY_TARGET:-}"
DEVICE="cpu"
ROS2_DISTRO="${BUDDY_ROS2_DISTRO:-}"
CLEAN=false
FORCE_NATIVE=false
PACKAGE=false
BUILD_ALL=false
REBUILD_BASE=false
VERSION="1.0.0"
PARALLEL="${BUDDY_PARALLEL_WORKERS:-$(nproc)}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--arch) TARGET="$2"; shift 2 ;;
    -d|--device) DEVICE="$2"; shift 2 ;;
    --ros-distro) ROS2_DISTRO="$2"; shift 2 ;;
    --all) BUILD_ALL=true; shift ;;
    -c) CLEAN=true; shift ;;
    --native) FORCE_NATIVE=true; shift ;;
    --rebuild-base) REBUILD_BASE=true; shift ;;
    -p|--package) PACKAGE=true; shift ;;
    -v) VERSION="$2"; shift 2 ;;
    -j) PARALLEL="$2"; shift 2 ;;
    -h|--help) usage ;;
    arm64|aarch64) TARGET="arm64"; shift ;;
    x86|x86_64) TARGET="x86"; shift ;;
    humble|jazzy) ROS2_DISTRO="$1"; shift ;;
    *) break ;;
  esac
done

# Auto-detect target arch from host if not explicitly set
if [[ -z "$TARGET" ]]; then
  case "$(uname -m)" in
    aarch64|arm64) TARGET="arm64" ;;
    *)             TARGET="x86" ;;
  esac
  echo "[INFO] Auto-detected target arch: $TARGET (from host)"
fi

# Auto-detect ROS distro from host OS if not explicitly set
if [[ -z "$ROS2_DISTRO" ]]; then
  ROS2_DISTRO=$(detect_ros_distro)
  echo "[INFO] Auto-detected ROS distro: $ROS2_DISTRO (from host OS)"
fi

case "$ROS2_DISTRO" in
  humble|jazzy) ;;
  *) die "Unknown ROS 2 distro: $ROS2_DISTRO" "Use --ros-distro humble or jazzy" ;;
esac

if [[ "$BUILD_ALL" == true ]]; then
  build_all
  exit $?
fi

case "$TARGET" in
  x86|x86_64)
    if [[ "$(uname -m)" == "x86_64" ]]; then
      build_native "$@"
    else
      build_docker x86_64 x86_64 amd64
    fi
    ;;
  arm64|aarch64)
    if [[ "$CLEAN" == true ]]; then
      echo "[CLEAN] Removing output/${ROS2_DISTRO}/aarch64/"
      rm -rf "$ROOT_DIR/output/${ROS2_DISTRO}/aarch64"
    fi
    if [[ "$(uname -m)" == "aarch64" || "$(uname -m)" == "arm64" ]]; then
      build_native "$@"
    else
      build_docker arm64 aarch64 arm64
    fi
    ;;
  *)
    die "Unknown target: $TARGET" "Use -t x86 or -t arm64"
    ;;
esac
