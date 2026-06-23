#!/bin/bash
# Build pre-installed Python venv for deb package (cross-arch safe).
# Versions locked in requirements.txt to avoid pip backtracking.
# BUDDY_PYTHON_VER: Python major.minor version (default: 3.12)
set -e

PYTHON_VER="${BUDDY_PYTHON_VER:-3.12}"

VENV_DIR="opt/buddy/services/llm/.venv"
REQ_FILE="opt/buddy/services/llm/requirements.txt"

mkdir -p "$VENV_DIR/bin" "$VENV_DIR/lib/python${PYTHON_VER}/site-packages"
printf '[virtualenv]\nhome = /usr/bin\ninclude-system-site-packages = true\nversion = %s\n' "$PYTHON_VER" \
  > "$VENV_DIR/pyvenv.cfg"
ln -sf /usr/bin/python3 "$VENV_DIR/bin/python3"
ln -sf python3 "$VENV_DIR/bin/python"

# Filter out system-installed packages
grep -v '^\s*#' "$REQ_FILE" | grep -v '^\s*$' | \
  while IFS= read -r line; do
    pkg="$(echo "$line" | sed 's/[>=<\[[:space:]].*//' | tr '[:upper:]' '[:lower:]' | tr '-' '_')"
    python3 -c "import $pkg" 2>/dev/null && continue
    echo "$line"
  done > /tmp/venv-requirements.txt

# Download wheels for aarch64 (locked versions = no backtracking)
pip download \
  --platform manylinux_2_17_aarch64 \
  --platform manylinux2014_aarch64 \
  --platform any \
  --python-version "$PYTHON_VER" \
  --only-binary=:all: \
  -r /tmp/venv-requirements.txt \
  -d "$VENV_DIR/wheels"

# Install from local wheels: --platform + direct .whl paths bypass host-platform check
pip install --quiet --upgrade --no-deps \
  --platform manylinux_2_17_aarch64 \
  --platform manylinux2014_aarch64 \
  --platform any \
  --python-version "$PYTHON_VER" \
  --only-binary=:all: \
  --target "$VENV_DIR/lib/python${PYTHON_VER}/site-packages" \
  "$VENV_DIR"/wheels/*.whl

# Generate activate script from template (VENV_DIR computed at runtime)
cp docker/activate.sh "$VENV_DIR/bin/activate"

# Fix shebangs for cross-arch
for script in "$VENV_DIR/bin/"*; do
  [ -f "$script" ] && [ "$script" != "$VENV_DIR/bin/activate" ] || continue
  case "$(head -1 "$script")" in \#!*) ;; *) continue ;; esac
  sed -i '1s|^#!.*|#!/usr/bin/env python3|' "$script"
done

sha256sum "$REQ_FILE" | awk '{print $1}' > "$VENV_DIR/.requirements.sha256"
touch "$VENV_DIR/.prebuilt"
