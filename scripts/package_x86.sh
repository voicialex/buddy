#!/usr/bin/env bash
# Package x86_64 build output into a .deb, matching arm64 packaging structure.
# Usage: ./scripts/package_x86.sh [-v VERSION] [-d DEVICE]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="1.0.0"
DEVICE="cpu"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -v|--version) VERSION="$2"; shift 2 ;;
    -d|--device)  DEVICE="$2"; shift 2 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

ARCH="x86_64"
INSTALL_DIR="$ROOT_DIR/output/$ARCH/install"
PREBUILT_DIR="$ROOT_DIR/prebuilt/$ARCH"
OUTPUT_DIR="$ROOT_DIR/output/$ARCH/deb"
DEB_ROOT="$OUTPUT_DIR/deb-root"

if [[ ! -d "$INSTALL_DIR/buddy_app" ]]; then
  echo "[ERROR] Build output not found at $INSTALL_DIR"
  echo "Run ./build.sh first."
  exit 1
fi

echo "[INFO] Packaging buddy-robot_${VERSION}_amd64.deb (device=$DEVICE)"

# ─── Clean and create structure ───────────────────────────────────────────────
rm -rf "$DEB_ROOT"
mkdir -p "$DEB_ROOT"/{opt/buddy/{bin,lib,lib/funasr,lib/sherpa,models,params,etc,scripts},lib/systemd/system,DEBIAN}

# ─── Binary ──────────────────────────────────────────────────────────────────
cp "$INSTALL_DIR/buddy_app/lib/buddy_app/buddy_main" "$DEB_ROOT/opt/buddy/bin/"

# ─── Shared libraries from colcon install ─────────────────────────────────────
find "$INSTALL_DIR" -name "*.so*" -type f -exec cp -n {} "$DEB_ROOT/opt/buddy/lib/" \;
find "$INSTALL_DIR" -name "*.so*" -type l -exec cp -nL {} "$DEB_ROOT/opt/buddy/lib/" \;

# ─── ROS 2 core libs (structure: ros2_core/<pkg>/lib/*.so*) ───────────────────
ros2_base="$PREBUILT_DIR/ros2_core"
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

# ─── FunASR (isolated: uses its own ORT copy for independent process) ─────────
cp -fP "$PREBUILT_DIR/onnxruntime/lib/"*.so* "$DEB_ROOT/opt/buddy/lib/funasr/" 2>/dev/null || true
cp -f "$PREBUILT_DIR/funasr/bin/funasr-wss-server" "$DEB_ROOT/opt/buddy/bin/" 2>/dev/null || true
cp -f "$PREBUILT_DIR/funasr/bin/funasr-wss-server-2pass" "$DEB_ROOT/opt/buddy/bin/" 2>/dev/null || true
cp -fP "$PREBUILT_DIR/funasr/lib/"*.so* "$DEB_ROOT/opt/buddy/lib/funasr/" 2>/dev/null || true

# ─── SentencePiece (GPU builds with MOSS-TTS only) ───────────────────────────
if [[ "$DEVICE" == "gpu" && -d "$PREBUILT_DIR/sentencepiece/lib" ]]; then
  cp -fP "$PREBUILT_DIR/sentencepiece/lib/"*.so* "$DEB_ROOT/opt/buddy/lib/"
fi

# ─── Params ──────────────────────────────────────────────────────────────────
cp -rL "$INSTALL_DIR/buddy_app/share/buddy_app/params/"* "$DEB_ROOT/opt/buddy/params/" 2>/dev/null || true

# ─── Scripts & service ────────────────────────────────────────────────────────
# run.sh goes to root (entry point)
[[ -f "$ROOT_DIR/docker/packaging/run.sh" ]] && cp "$ROOT_DIR/docker/packaging/run.sh" "$DEB_ROOT/opt/buddy/run.sh"
# buddy.env goes to etc/
[[ -f "$ROOT_DIR/docker/packaging/buddy.env" ]] && cp "$ROOT_DIR/docker/packaging/buddy.env" "$DEB_ROOT/opt/buddy/etc/buddy.env"
# Other scripts to scripts/
for f in start_asr_server.sh start_tts_server.sh; do
  [[ -f "$ROOT_DIR/docker/packaging/$f" ]] && cp "$ROOT_DIR/docker/packaging/$f" "$DEB_ROOT/opt/buddy/scripts/"
done
[[ -f "$ROOT_DIR/scripts/start_llm_server.sh" ]] && cp "$ROOT_DIR/scripts/start_llm_server.sh" "$DEB_ROOT/opt/buddy/scripts/"
[[ -f "$ROOT_DIR/scripts/common.sh" ]] && cp "$ROOT_DIR/scripts/common.sh" "$DEB_ROOT/opt/buddy/scripts/"
[[ -f "$ROOT_DIR/scripts/status.sh" ]] && cp "$ROOT_DIR/scripts/status.sh" "$DEB_ROOT/opt/buddy/scripts/"

# Python services
mkdir -p "$DEB_ROOT/opt/buddy/services/llm" "$DEB_ROOT/opt/buddy/services/tts"
cp -r "$ROOT_DIR/services/llm/"* "$DEB_ROOT/opt/buddy/services/llm/" 2>/dev/null || true
cp -r "$ROOT_DIR/services/tts/"* "$DEB_ROOT/opt/buddy/services/tts/" 2>/dev/null || true
# Exclude .venv from package
rm -rf "$DEB_ROOT/opt/buddy/services/llm/.venv" "$DEB_ROOT/opt/buddy/services/tts/.venv"
rm -rf "$DEB_ROOT/opt/buddy/services/llm/backends/__pycache__"

# LLM startup script
[[ -f "$ROOT_DIR/scripts/start_llm_server.sh" ]] && cp "$ROOT_DIR/scripts/start_llm_server.sh" "$DEB_ROOT/opt/buddy/scripts/"

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
 All-in-one package including audio pipeline, vision,
 brain state machine, and cloud inference client.
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
