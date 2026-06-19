#!/bin/bash
# Build pre-installed Python venv for deb package (cross-arch safe).
# Versions locked in requirements.txt to avoid pip backtracking.
set -e

VENV_DIR="opt/buddy/services/llm/.venv"
REQ_FILE="opt/buddy/services/llm/requirements.txt"

mkdir -p "$VENV_DIR/bin" "$VENV_DIR/lib/python3.12/site-packages"
printf '[virtualenv]\nhome = /usr/bin\ninclude-system-site-packages = true\nversion = 3.12\n' \
  > "$VENV_DIR/pyvenv.cfg"
ln -s /usr/bin/python3 "$VENV_DIR/bin/python3"
ln -s python3 "$VENV_DIR/bin/python"

# Filter out system-installed packages
grep -v '^\s*#' "$REQ_FILE" | grep -v '^\s*$' | \
  while IFS= read -r line; do
    pkg="$(echo "$line" | sed 's/[>=<\[[:space:]].*//' | tr '[:upper:]' '[:lower:]' | tr '-' '_')"
    python3 -c "import $pkg" 2>/dev/null && continue
    echo "$line"
  done > /tmp/venv-requirements.txt

# Download wheels for aarch64 (locked versions = no backtracking)
pip download --quiet \
  --platform manylinux_2_17_aarch64 \
  --platform manylinux2014_aarch64 \
  --platform any \
  --python-version 3.12 --implementation cp --abi cp312 \
  --only-binary=:all: \
  -r /tmp/venv-requirements.txt \
  -d "$VENV_DIR/wheels"

# Install from local wheels (no-deps: versions already locked)
pip install --quiet --no-index --find-links="$VENV_DIR/wheels" \
  --platform manylinux_2_17_aarch64 \
  --platform any \
  --python-version 3.12 --implementation cp --abi cp312 \
  --only-binary=:all: \
  --target "$VENV_DIR/lib/python3.12/site-packages" \
  --no-deps \
  -r /tmp/venv-requirements.txt

# Generate activate script from template
cp docker/activate.sh "$VENV_DIR/bin/activate"
sed -i "s|__VENV_DIR__|$VENV_DIR|g" "$VENV_DIR/bin/activate"

# Fix shebangs for cross-arch
for script in "$VENV_DIR/bin/"*; do
  [ -f "$script" ] && [ "$script" != "$VENV_DIR/bin/activate" ] || continue
  case "$(head -1 "$script")" in \#!*) ;; *) continue ;; esac
  sed -i '1s|^#!.*|#!/usr/bin/env python3|' "$script"
done

# Cleanup wheels (save space in deb)
rm -rf "$VENV_DIR/wheels"

sha256sum "$REQ_FILE" | awk '{print $1}' > "$VENV_DIR/.requirements.sha256"
