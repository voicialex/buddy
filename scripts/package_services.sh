#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

VERSION="1.0.0"
ARCH_RAW="$(uname -m)"
SERVICES="all"

usage() {
  cat <<'EOF'
Usage: ./scripts/package_services.sh [options]

Options:
  --arch <x86_64|aarch64|arm64>   Target arch (default: host)
  --version <ver>                 Version in output file names (default: 1.0.0)
  --services <list>               all | llm | funasr | chattts | llm,funasr,...
  -h, --help                      Show help

Output:
  output/<arch>/services/
    buddy-service-llm_<ver>_<arch>.tar.gz
    buddy-service-funasr_<ver>_<arch>.tar.gz
    buddy-service-chattts_<ver>_<arch>.tar.gz
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --arch) ARCH_RAW="$2"; shift 2 ;;
    --version|-v) VERSION="$2"; shift 2 ;;
    --services) SERVICES="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "[ERROR] Unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

case "$ARCH_RAW" in
  x86_64|amd64) ARCH="x86_64" ;;
  aarch64|arm64) ARCH="aarch64" ;;
  *) echo "[ERROR] Unsupported arch: $ARCH_RAW" >&2; exit 1 ;;
esac

OUT_DIR="$ROOT_DIR/output/$ARCH/services"
PREBUILT_DIR="$ROOT_DIR/prebuilt/$ARCH"
mkdir -p "$OUT_DIR"

want_service() {
  local name="$1"
  if [[ "$SERVICES" == "all" ]]; then
    return 0
  fi
  [[ ",$SERVICES," == *",$name,"* ]]
}

build_llm_pkg() {
  local tmp
  tmp="$(mktemp -d)"
  mkdir -p "$tmp/opt/buddy/scripts" "$tmp/opt/buddy/services/llm"

  cp "$ROOT_DIR/scripts/start_llm_server.sh" "$tmp/opt/buddy/scripts/"
  cp "$ROOT_DIR/scripts/common.sh" "$tmp/opt/buddy/scripts/"
  cp -r "$ROOT_DIR/services/llm/"* "$tmp/opt/buddy/services/llm/"
  rm -rf "$tmp/opt/buddy/services/llm/.venv" \
         "$tmp/opt/buddy/services/llm/tests" \
         "$tmp/opt/buddy/services/llm/__pycache__" \
         "$tmp/opt/buddy/services/llm/backends/__pycache__"

  if [[ -d "$ROOT_DIR/docker/rkllm_server" ]]; then
    mkdir -p "$tmp/opt/buddy/rkllm_server"
    cp -r "$ROOT_DIR/docker/rkllm_server/"* "$tmp/opt/buddy/rkllm_server/" 2>/dev/null || true
  fi
  if [[ -d "$PREBUILT_DIR/rkllm/lib" ]]; then
    mkdir -p "$tmp/opt/buddy/rkllm_server/lib"
    cp -fP "$PREBUILT_DIR/rkllm/lib/"*.so* "$tmp/opt/buddy/rkllm_server/lib/" 2>/dev/null || true
  fi

  chmod 755 "$tmp/opt/buddy/scripts/start_llm_server.sh" "$tmp/opt/buddy/scripts/common.sh"
  tar czf "$OUT_DIR/buddy-service-llm_${VERSION}_${ARCH}.tar.gz" -C "$tmp" .
  rm -rf "$tmp"
}

build_funasr_pkg() {
  local tmp
  tmp="$(mktemp -d)"
  mkdir -p "$tmp/opt/buddy/scripts" "$tmp/opt/buddy/bin" "$tmp/opt/buddy/lib/funasr"

  cp "$ROOT_DIR/docker/packaging/start_asr_server.sh" "$tmp/opt/buddy/scripts/start_asr_server.sh"
  chmod 755 "$tmp/opt/buddy/scripts/start_asr_server.sh"

  cp -f "$PREBUILT_DIR/funasr/bin/funasr-wss-server" "$tmp/opt/buddy/bin/" 2>/dev/null || true
  cp -f "$PREBUILT_DIR/funasr/bin/funasr-wss-server-2pass" "$tmp/opt/buddy/bin/" 2>/dev/null || true
  cp -fP "$PREBUILT_DIR/funasr/lib/"*.so* "$tmp/opt/buddy/lib/funasr/" 2>/dev/null || true
  cp -fP "$PREBUILT_DIR/onnxruntime/lib/"*.so* "$tmp/opt/buddy/lib/funasr/" 2>/dev/null || true

  tar czf "$OUT_DIR/buddy-service-funasr_${VERSION}_${ARCH}.tar.gz" -C "$tmp" .
  rm -rf "$tmp"
}

build_chattts_pkg() {
  local tmp
  tmp="$(mktemp -d)"
  mkdir -p "$tmp/opt/buddy/scripts" "$tmp/opt/buddy/services/tts"

  cp "$ROOT_DIR/docker/packaging/start_tts_server.sh" "$tmp/opt/buddy/scripts/start_tts_server.sh"
  cp -r "$ROOT_DIR/services/tts/"* "$tmp/opt/buddy/services/tts/"
  rm -rf "$tmp/opt/buddy/services/tts/.venv" "$tmp/opt/buddy/services/tts/__pycache__"
  chmod 755 "$tmp/opt/buddy/scripts/start_tts_server.sh"

  tar czf "$OUT_DIR/buddy-service-chattts_${VERSION}_${ARCH}.tar.gz" -C "$tmp" .
  rm -rf "$tmp"
}

echo "[INFO] Packaging optional services (arch=$ARCH version=$VERSION)"
if want_service "llm"; then
  build_llm_pkg
  echo "[OK] $(basename "$OUT_DIR/buddy-service-llm_${VERSION}_${ARCH}.tar.gz")"
fi
if want_service "funasr"; then
  build_funasr_pkg
  echo "[OK] $(basename "$OUT_DIR/buddy-service-funasr_${VERSION}_${ARCH}.tar.gz")"
fi
if want_service "chattts"; then
  build_chattts_pkg
  echo "[OK] $(basename "$OUT_DIR/buddy-service-chattts_${VERSION}_${ARCH}.tar.gz")"
fi

echo "[INFO] Output dir: $OUT_DIR"
