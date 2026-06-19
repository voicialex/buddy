#!/usr/bin/env bash
# Stage thirdparty build outputs into buddy/prebuilt/<arch>/
#
# Usage:
#   ./scripts/build_thirdparty.sh              # host arch
#   ./scripts/build_thirdparty.sh --arch aarch64  # arm64
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
THIRDPARTY_DIR="$(cd "$PROJECT_DIR/../thirdparty" && pwd)"

ARCH="$(uname -m)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch) ARCH="$2"; shift 2 ;;
        *) ARCH="$1"; shift ;;
    esac
done

case "$ARCH" in
    arm64) ARCH="aarch64" ;;
    x86|x86_64) ARCH="x86_64" ;;
esac

PREBUILT_DIR="$PROJECT_DIR/prebuilt/$ARCH"
THIRDPARTY_OUTPUT="$THIRDPARTY_DIR/output/$ARCH"

echo "[INFO] Staging thirdparty for $ARCH"
echo "[INFO] Source: $THIRDPARTY_OUTPUT"
echo "[INFO] Target: $PREBUILT_DIR"

mkdir -p "$PREBUILT_DIR"

for dep in opencv libcurl; do
    src="$THIRDPARTY_OUTPUT/$dep"
    dst="$PREBUILT_DIR/$dep"

    if [[ ! -d "$src" ]]; then
        echo "[WARN] $dep not found at $src — build thirdparty first:"
        echo "  cd ../thirdparty && ./build.sh -t $ARCH $dep"
        continue
    fi

    if [[ ! -d "$dst" ]] || [[ "$src/lib" -nt "$dst/lib" ]]; then
        echo "[INFO] Copying $dep → prebuilt/$ARCH/$dep"
        rm -rf "$dst"
        cp -a "$src" "$dst"
    else
        echo "[INFO] Using cached $dep from prebuilt/$ARCH/$dep"
    fi
done

echo "[OK] Thirdparty staging complete"
