#!/usr/bin/env bash
# Package x86_64 build output into a .deb, matching arm64 packaging structure.
# Usage: ./scripts/package_x86.sh [-v VERSION] [-d DEVICE] [--ros-distro humble|jazzy]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="1.0.0"
DEVICE="cpu"
ROS2_DISTRO="humble"
PYTHON_VER="3.12"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -v|--version) VERSION="$2"; shift 2 ;;
    -d|--device)  DEVICE="$2"; shift 2 ;;
    --ros-distro) ROS2_DISTRO="$2"; shift 2 ;;
    --python-ver) PYTHON_VER="$2"; shift 2 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

case "$ROS2_DISTRO" in
  humble|jazzy) ;;
  *) echo "[ERROR] Unknown ROS 2 distro: $ROS2_DISTRO"; exit 1 ;;
esac

ARCH="x86_64"
INSTALL_DIR="$ROOT_DIR/output/${ROS2_DISTRO}/$ARCH/install"
PREBUILT_DIR="$ROOT_DIR/prebuilt/$ARCH"
OUTPUT_DIR="$ROOT_DIR/output/${ROS2_DISTRO}/$ARCH/deb"
DEB_ROOT="$OUTPUT_DIR/deb-root"

if [[ ! -d "$INSTALL_DIR/buddy_app" ]]; then
  echo "[ERROR] Build output not found at $INSTALL_DIR"
  echo "Run ./build.sh first."
  exit 1
fi

echo "[INFO] Packaging buddy-robot_${VERSION}_amd64.deb (device=$DEVICE)"

# ─── Clean and create structure ───────────────────────────────────────────────
rm -rf "$DEB_ROOT"
mkdir -p "$DEB_ROOT"/{opt/buddy/{bin,lib,lib/sherpa,models,params,etc,scripts},lib/systemd/system,DEBIAN}

# ─── Binary ──────────────────────────────────────────────────────────────────
cp "$INSTALL_DIR/buddy_app/lib/buddy_app/buddy_main" "$DEB_ROOT/opt/buddy/bin/"

# ─── Shared libraries from colcon install ─────────────────────────────────────
find "$INSTALL_DIR" -name "*.so*" -type f -exec cp -n {} "$DEB_ROOT/opt/buddy/lib/" \;
find "$INSTALL_DIR" -name "*.so*" -type l -exec cp -nL {} "$DEB_ROOT/opt/buddy/lib/" \;

# ─── ROS 2 core libs (structure: ros2_core/<distro>/<pkg>/lib/*.so*) ──────────
ros2_base="$PREBUILT_DIR/ros2_core/$ROS2_DISTRO"
if [[ -d "$ros2_base" ]]; then
  find "$ros2_base" -path "*/lib/*.so*" -type f -exec cp -n {} "$DEB_ROOT/opt/buddy/lib/" \;
  find "$ros2_base" -path "*/lib/*.so*" -type l -exec cp -nP {} "$DEB_ROOT/opt/buddy/lib/" \;
fi

# ─── Sherpa-ONNX (isolated: bundles its own ORT with different SONAME) ────────
cp -fP "$PREBUILT_DIR/sherpa-onnx/lib/"*.so* "$DEB_ROOT/opt/buddy/lib/sherpa/" 2>/dev/null || true

# ─── OpenCV ───────────────────────────────────────────────────────────────────
cp -fP "$PREBUILT_DIR/opencv/lib/"*.so* "$DEB_ROOT/opt/buddy/lib/" 2>/dev/null || true

# ─── ONNX Runtime (device-dependent) ─────────────────────────────────────────
# cpu → onnxruntime (CPU)
# gpu → onnxruntime-gpu (CUDA + TensorRT providers)
if [[ "$DEVICE" == "gpu" && -d "$PREBUILT_DIR/onnxruntime-gpu/lib" ]]; then
  cp -fP "$PREBUILT_DIR/onnxruntime-gpu/lib/"*.so* "$DEB_ROOT/opt/buddy/lib/"
else
  cp -fP "$PREBUILT_DIR/onnxruntime/lib/"*.so* "$DEB_ROOT/opt/buddy/lib/" 2>/dev/null || true
fi

# ─── SentencePiece (GPU builds with MOSS-TTS only) ───────────────────────────
if [[ "$DEVICE" == "gpu" && -d "$PREBUILT_DIR/sentencepiece/lib" ]]; then
  cp -fP "$PREBUILT_DIR/sentencepiece/lib/"*.so* "$DEB_ROOT/opt/buddy/lib/"
fi

# ─── Strip symbols (deb-only; build output keeps full symbols) ───────────────
# Strip buddy components + opencv. Third-party (onnxruntime/sherpa/ros2_core)
# ships already-stripped or has internal layout strip-all would corrupt.
strip_args=()
for f in \
  "$DEB_ROOT/opt/buddy/bin/buddy_main" \
  "$DEB_ROOT/opt/buddy/lib/libaudio_component.so" \
  "$DEB_ROOT/opt/buddy/lib/libbrain_component.so" \
  "$DEB_ROOT/opt/buddy/lib/libvision_component.so" \
  "$DEB_ROOT/opt/buddy/lib/libllm_bridge_component.so" \
  "$DEB_ROOT/opt/buddy/lib/libcomponent_manager.so"
do
  [[ -f "$f" ]] && strip_args+=("$f")
done
[[ ${#strip_args[@]} -gt 0 ]] && find "$DEB_ROOT/opt/buddy/lib/" -maxdepth 1 -name "libopencv_*.so*" -type f -exec strip --strip-all {} + 2>/dev/null || true
[[ ${#strip_args[@]} -gt 0 ]] && strip --strip-all "${strip_args[@]}" 2>/dev/null || true

# ─── Params ──────────────────────────────────────────────────────────────────
cp -rL "$INSTALL_DIR/buddy_app/share/buddy_app/params/"* "$DEB_ROOT/opt/buddy/params/" 2>/dev/null || true

# ─── Scripts ──────────────────────────────────────────────────────────────────
# run.sh goes to root (entry point)
[[ -f "$ROOT_DIR/docker/packaging/run.sh" ]] && cp "$ROOT_DIR/docker/packaging/run.sh" "$DEB_ROOT/opt/buddy/run.sh"
# buddy.env goes to etc/
[[ -f "$ROOT_DIR/docker/packaging/buddy.env" ]] && cp "$ROOT_DIR/docker/packaging/buddy.env" "$DEB_ROOT/opt/buddy/etc/buddy.env"
[[ -f "$ROOT_DIR/scripts/common.sh" ]] && cp "$ROOT_DIR/scripts/common.sh" "$DEB_ROOT/opt/buddy/scripts/"
[[ -f "$ROOT_DIR/scripts/status.sh" ]] && cp "$ROOT_DIR/scripts/status.sh" "$DEB_ROOT/opt/buddy/scripts/"
[[ -f "$ROOT_DIR/scripts/start_llm_server.sh" ]] && cp "$ROOT_DIR/scripts/start_llm_server.sh" "$DEB_ROOT/opt/buddy/scripts/"
if [[ -d "$ROOT_DIR/services/llm" ]]; then
  mkdir -p "$DEB_ROOT/opt/buddy/services/llm"
  cp -r "$ROOT_DIR/services/llm/"* "$DEB_ROOT/opt/buddy/services/llm/"
  rm -rf "$DEB_ROOT/opt/buddy/services/llm/tests"
  find "$DEB_ROOT/opt/buddy/services/llm" -name __pycache__ -type d -exec rm -rf {} + 2>/dev/null || true

  # Build pre-installed Python venv with wheels
  echo "[INFO] Building pre-installed Python venv..."
  PIP_INDEX="https://mirrors.aliyun.com/pypi/simple/"
  WHEELS_DIR="$DEB_ROOT/opt/buddy/services/llm/wheels"
  VENV_DIR="$DEB_ROOT/opt/buddy/services/llm/.venv"
  SITE_PACKAGES="$VENV_DIR/lib/python${PYTHON_VER}/site-packages"

  mkdir -p "$WHEELS_DIR" "$VENV_DIR/bin" "$SITE_PACKAGES"

  # Download wheels
  pip download -i "$PIP_INDEX" --trusted-host mirrors.aliyun.com \
    --platform manylinux2014_x86_64 --platform manylinux_2_17_x86_64 \
    --platform any --platform linux_x86_64 \
    --python-version "$PYTHON_VER" --only-binary=:all: \
    -r "$DEB_ROOT/opt/buddy/services/llm/requirements.txt" \
    -d "$WHEELS_DIR/" \
  && pip download -i "$PIP_INDEX" --trusted-host mirrors.aliyun.com \
    --platform any --python-version "$PYTHON_VER" --only-binary=:all: \
    -r "$DEB_ROOT/opt/buddy/services/llm/requirements.txt" \
    -d "$WHEELS_DIR/" \
  || echo "[WARN] Some wheels download failed"

  # Create minimal venv structure
  printf '[virtualenv]\nhome = /usr/bin\ninclude-system-site-packages = true\nversion = %s\n' "$PYTHON_VER" \
    > "$VENV_DIR/pyvenv.cfg"
  ln -sf /usr/bin/python3 "$VENV_DIR/bin/python3"
  ln -sf python3 "$VENV_DIR/bin/python"

  # Install wheels directly: --platform + direct .whl paths bypass host-platform check
  pip install --quiet --no-deps \
    --platform manylinux2014_x86_64 \
    --platform manylinux_2_17_x86_64 \
    --platform any \
    --python-version "$PYTHON_VER" \
    --only-binary=:all: \
    --target "$SITE_PACKAGES" \
    "$WHEELS_DIR"/*.whl \
  || echo "[WARN] Some packages install failed"

  # Strip build-time artifacts from venv (saves ~5-10MB)
  # 保留 dist-info — importlib.metadata.version() 运行时需要查包版本
  find "$SITE_PACKAGES" -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
  find "$SITE_PACKAGES" -name "*.egg-info" -type d -exec rm -rf {} + 2>/dev/null || true
  find "$SITE_PACKAGES" -name "*.pyc" -delete 2>/dev/null || true

  # Create activate script
  cat > "$VENV_DIR/bin/activate" <<'ACTIVATE_EOF'
deactivate () {
    if [ -n "${_OLD_VIRTUAL_PATH:-}" ] ; then
        PATH="$_OLD_VIRTUAL_PATH"; export PATH; unset _OLD_VIRTUAL_PATH
    fi
    if [ -n "${BASH:-}" -o -n "${ZSH_VERSION:-}" ] ; then hash -r 2>/dev/null; fi
    if [ -n "${_OLD_VIRTUAL_PS1:-}" ] ; then
        PS1="$_OLD_VIRTUAL_PS1"; export PS1; unset _OLD_VIRTUAL_PS1
    fi
    unset VIRTUAL_ENV
    unset VIRTUAL_ENV_PROMPT
    if [ ! "$1" = "nondestructive" ] ; then unset -f deactivate; fi
}
deactivate nondestructive
VIRTUAL_ENV="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export VIRTUAL_ENV
_OLD_VIRTUAL_PATH="$PATH"
PATH="$VIRTUAL_ENV/bin:$PATH"
export PATH
if [ -z "${VIRTUAL_ENV_DISABLE_PROMPT:-}" ] ; then
    _OLD_VIRTUAL_PS1="${PS1:-}"
    PS1="(.venv) ${PS1:-}"
    export PS1
    VIRTUAL_ENV_PROMPT="(.venv) "
    export VIRTUAL_ENV_PROMPT
fi
if [ -n "${BASH:-}" -o -n "${ZSH_VERSION:-}" ] ; then hash -r 2>/dev/null; fi
ACTIVATE_EOF

  # Write requirements hash
  sha256sum "$DEB_ROOT/opt/buddy/services/llm/requirements.txt" | awk '{print $1}' \
    > "$VENV_DIR/.requirements.sha256"
  touch "$VENV_DIR/.prebuilt"
fi
if [[ -d "$ROOT_DIR/docker/rkllm_server" ]]; then
  mkdir -p "$DEB_ROOT/opt/buddy/rkllm_server"
  cp -r "$ROOT_DIR/docker/rkllm_server/"* "$DEB_ROOT/opt/buddy/rkllm_server/"
  find "$DEB_ROOT/opt/buddy/rkllm_server" -name __pycache__ -type d -exec rm -rf {} + 2>/dev/null || true
  if [[ -f "$DEB_ROOT/opt/buddy/rkllm_server/requirements.txt" ]]; then
    rkllm_wheels="$DEB_ROOT/opt/buddy/rkllm_server/wheels"
    rkllm_venv="$DEB_ROOT/opt/buddy/rkllm_server/.venv"
    rkllm_site="$rkllm_venv/lib/python${PYTHON_VER}/site-packages"
    mkdir -p "$rkllm_wheels" "$rkllm_venv/bin" "$rkllm_site"
    pip download --platform manylinux2014_x86_64 --platform manylinux_2_17_x86_64 \
      --platform any --platform linux_x86_64 \
      --python-version "$PYTHON_VER" --only-binary=:all: \
      -r "$DEB_ROOT/opt/buddy/rkllm_server/requirements.txt" -d "$rkllm_wheels/" || echo "[WARN] rkllm flask wheels download failed"
    printf '[virtualenv]\nhome = /usr/bin\ninclude-system-site-packages = true\nversion = %s\n' "$PYTHON_VER" \
      > "$rkllm_venv/pyvenv.cfg"
    ln -sf /usr/bin/python3 "$rkllm_venv/bin/python3"
    ln -sf python3 "$rkllm_venv/bin/python"
    pip install --quiet --no-deps --platform manylinux2014_x86_64 --platform manylinux_2_17_x86_64 \
      --platform any \
      --python-version "$PYTHON_VER" --only-binary=:all: \
      --target "$rkllm_site" "$rkllm_wheels"/*.whl || echo "[WARN] rkllm flask install failed"
    find "$rkllm_site" -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
    find "$rkllm_site" -name "*.egg-info" -type d -exec rm -rf {} + 2>/dev/null || true
    find "$rkllm_site" -name "*.pyc" -delete 2>/dev/null || true
    cp "$VENV_DIR/bin/activate" "$rkllm_venv/bin/activate" 2>/dev/null || true
    sha256sum "$DEB_ROOT/opt/buddy/rkllm_server/requirements.txt" | awk '{print $1}' > "$rkllm_venv/.requirements.sha256"
    touch "$rkllm_venv/.prebuilt"
  fi
fi

[[ -f "$ROOT_DIR/docker/packaging/buddy.service" ]] && \
  cp "$ROOT_DIR/docker/packaging/buddy.service" "$DEB_ROOT/lib/systemd/system/"

# ─── DEBIAN control ──────────────────────────────────────────────────────────
cat > "$DEB_ROOT/DEBIAN/control" <<EOF
Package: buddy-robot
Version: ${VERSION}
Architecture: amd64
Maintainer: buddy-dev <dev@example.com>
Depends: libasound2, libcurl4, libstdc++6, python3, python3-venv, curl
Description: Buddy Robot - AI companion runtime (x86_64, device=${DEVICE})
 Core runtime package for buddy_main and direct dependencies.
 Optional ASR/TTS/LLM services are packaged separately.
EOF

for f in postinst prerm postrm; do
  if [[ -f "$ROOT_DIR/docker/packaging/$f" ]]; then
    cp "$ROOT_DIR/docker/packaging/$f" "$DEB_ROOT/DEBIAN/$f"
    chmod 755 "$DEB_ROOT/DEBIAN/$f"
  fi
done

# ─── Permissions ─────────────────────────────────────────────────────────────
chmod 755 "$DEB_ROOT/opt/buddy/bin/buddy_main" "$DEB_ROOT/opt/buddy/run.sh"
find "$DEB_ROOT/opt/buddy/scripts" -type f -exec chmod 755 {} \; 2>/dev/null || true

# ─── Build .deb ──────────────────────────────────────────────────────────────
mkdir -p "$OUTPUT_DIR"
dpkg-deb --build "$DEB_ROOT" "$OUTPUT_DIR/buddy-robot_${VERSION}_amd64_${DEVICE}.deb"

# ─── Models tarball ──────────────────────────────────────────────────────────
MODELS_TAR="$OUTPUT_DIR/buddy-models_${VERSION}_${DEVICE}.tar.gz"
if [[ -f "$MODELS_TAR" ]]; then
  echo "[SKIP] Models tarball already exists: $(du -sh "$MODELS_TAR" | cut -f1)"
elif [[ -d "$ROOT_DIR/models" ]]; then
  excludes=(--exclude="ollama" --exclude="rkllm")
  # x86 never needs RKNN ASR models (arm64 NPU only)
  excludes+=(--exclude="zipformer-rknn")
  # Exclude redundant archive files (directories already extracted)
  excludes+=(--exclude="*.tar" --exclude="*.tar.*" --exclude="*.tgz")
  # gpu builds can also skip FunASR server models (uses GPU ORT directly)
  if [[ "$DEVICE" == "gpu" ]]; then
    excludes+=(--exclude="funasr-paraformer-zh-offline" --exclude="funasr-paraformer-zh-online" --exclude="funasr-vad")
  fi
  tar czf "$MODELS_TAR" -C "$ROOT_DIR/models" "${excludes[@]}" .
fi

echo ""
echo "[OK] Package: $OUTPUT_DIR/buddy-robot_${VERSION}_amd64_${DEVICE}.deb ($(du -sh "$OUTPUT_DIR/buddy-robot_${VERSION}_amd64_${DEVICE}.deb" | cut -f1))"
[[ -f "$MODELS_TAR" ]] && echo "[OK] Models:  $MODELS_TAR ($(du -sh "$MODELS_TAR" | cut -f1))"
