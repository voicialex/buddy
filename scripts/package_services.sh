#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

VERSION="1.0.0"
ARCH_RAW="$(uname -m)"
SERVICES="llm"
ROS2_DISTRO="humble"
PYTHON_VER="3.12"

usage() {
  cat <<'EOF'
Usage: ./scripts/package_services.sh [options]

Options:
  --arch <x86_64|aarch64|arm64>   Target arch (default: host)
  --version <ver>                 Version in output file names (default: 1.0.0)
  --ros-distro <humble|jazzy>     ROS 2 distro (default: humble)
  --services <list>               all | llm | funasr | chattts | llm,funasr,...
  --python-ver <3.10|3.12>        Target Python version (default: 3.12, use 3.10 for Humble)
  -h, --help                      Show help

Output:
  output/<distro>/<arch>/services/
    buddy-service-llm_<ver>_<arch>.tar.gz
    buddy-service-funasr_<ver>_<arch>.tar.gz
    buddy-service-chattts_<ver>_<arch>.tar.gz
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --arch) ARCH_RAW="$2"; shift 2 ;;
    --version|-v) VERSION="$2"; shift 2 ;;
    --ros-distro) ROS2_DISTRO="$2"; shift 2 ;;
    --services) SERVICES="$2"; shift 2 ;;
    --python-ver) PYTHON_VER="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "[ERROR] Unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

case "$ROS2_DISTRO" in
  humble|jazzy) ;;
  *) echo "[ERROR] Unknown ROS 2 distro: $ROS2_DISTRO" >&2; exit 1 ;;
esac

case "$ARCH_RAW" in
  x86_64|amd64) ARCH="x86_64" ;;
  aarch64|arm64) ARCH="aarch64" ;;
  *) echo "[ERROR] Unsupported arch: $ARCH_RAW" >&2; exit 1 ;;
esac

OUT_DIR="$ROOT_DIR/output/${ROS2_DISTRO}/$ARCH/services"
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
  rm -rf "$tmp/opt/buddy/services/llm/tests"
  find "$tmp/opt/buddy/services/llm" -name __pycache__ -type d -exec rm -rf {} + 2>/dev/null || true

  # Build pre-installed Python venv with offline wheels
  echo "[INFO] Building pre-installed Python venv ($ARCH)..."
  local wheels_dir="$tmp/opt/buddy/services/llm/wheels"
  local venv_dir="$tmp/opt/buddy/services/llm/.venv"
  local req_file="$tmp/opt/buddy/services/llm/requirements.txt"
  local site_packages="$venv_dir/lib/python${PYTHON_VER}/site-packages"
  mkdir -p "$wheels_dir" "$venv_dir/bin" "$site_packages"

  # Resolve pip platform flags per architecture
  local plat_flags=(--platform any)
  case "$ARCH" in
    x86_64)
      plat_flags+=(--platform manylinux2014_x86_64 --platform manylinux_2_17_x86_64 --platform linux_x86_64)
      ;;
    aarch64)
      plat_flags+=(--platform manylinux_2_17_aarch64 --platform manylinux2014_aarch64)
      ;;
  esac

  # Download all dependencies as wheels for target arch (incl. transitive deps)
  PIP_INDEX="https://mirrors.aliyun.com/pypi/simple/"
  pip download \
    -i "$PIP_INDEX" --trusted-host mirrors.aliyun.com \
    "${plat_flags[@]}" \
    --python-version "$PYTHON_VER" \
    --only-binary=:all: \
    -r "$req_file" \
    -d "$wheels_dir/" || echo "[WARN] Some wheels download failed"

  # Create minimal venv
  printf '[virtualenv]\nhome = /usr/bin\ninclude-system-site-packages = true\nversion = %s\n' "$PYTHON_VER" \
    > "$venv_dir/pyvenv.cfg"
  ln -sf /usr/bin/python3 "$venv_dir/bin/python3"
  ln -sf python3 "$venv_dir/bin/python"

  # Install wheels directly: --platform + direct .whl paths bypass host-platform check
  pip install --quiet --no-deps \
    "${plat_flags[@]}" \
    --python-version "$PYTHON_VER" \
    --only-binary=:all: \
    --target "$site_packages" \
    "$wheels_dir"/*.whl \
    || echo "[WARN] Some packages install failed"

  # Create activate script
  cat > "$venv_dir/bin/activate" <<'ACTIVATE_EOF'
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

  # Write requirements hash and prebuilt marker
  sha256sum "$req_file" | awk '{print $1}' > "$venv_dir/.requirements.sha256"
  touch "$venv_dir/.prebuilt"

  if [[ -d "$ROOT_DIR/docker/rkllm_server" ]]; then
    mkdir -p "$tmp/opt/buddy/rkllm_server"
    cp -r "$ROOT_DIR/docker/rkllm_server/"* "$tmp/opt/buddy/rkllm_server/" 2>/dev/null || true
  fi
  if [[ -d "$PREBUILT_DIR/rkllm/lib" ]]; then
    mkdir -p "$tmp/opt/buddy/rkllm_server/lib"
    cp -fP "$PREBUILT_DIR/rkllm/lib/"*.so* "$tmp/opt/buddy/rkllm_server/lib/" 2>/dev/null || true
  fi
  # Pre-build rkllm_server venv (flask only, pure Python)
  if [[ -f "$tmp/opt/buddy/rkllm_server/requirements.txt" ]]; then
    local rkllm_wheels="$tmp/opt/buddy/rkllm_server/wheels"
    local rkllm_venv="$tmp/opt/buddy/rkllm_server/.venv"
    local rkllm_site="$rkllm_venv/lib/python${PYTHON_VER}/site-packages"
    mkdir -p "$rkllm_wheels" "$rkllm_venv/bin" "$rkllm_site"
    pip download -i "$PIP_INDEX" --trusted-host mirrors.aliyun.com \
      "${plat_flags[@]}" --python-version "$PYTHON_VER" --only-binary=:all: \
      -r "$tmp/opt/buddy/rkllm_server/requirements.txt" -d "$rkllm_wheels/" || echo "[WARN] rkllm flask wheels download failed"
    printf '[virtualenv]\nhome = /usr/bin\ninclude-system-site-packages = true\nversion = %s\n' "$PYTHON_VER" \
      > "$rkllm_venv/pyvenv.cfg"
    ln -sf /usr/bin/python3 "$rkllm_venv/bin/python3"
    ln -sf python3 "$rkllm_venv/bin/python"
    pip install --quiet --no-deps "${plat_flags[@]}" --python-version "$PYTHON_VER" --only-binary=:all: \
      --target "$rkllm_site" "$rkllm_wheels"/*.whl || echo "[WARN] rkllm flask install failed"
    cp "$venv_dir/bin/activate" "$rkllm_venv/bin/activate" 2>/dev/null || true
    sha256sum "$tmp/opt/buddy/rkllm_server/requirements.txt" | awk '{print $1}' > "$rkllm_venv/.requirements.sha256"
    touch "$rkllm_venv/.prebuilt"
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
