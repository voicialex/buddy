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

# Generate activate script (inline, no external template needed)
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

# Fix shebangs for cross-arch
for script in "$VENV_DIR/bin/"*; do
  [ -f "$script" ] && [ "$script" != "$VENV_DIR/bin/activate" ] || continue
  case "$(head -1 "$script")" in \#!*) ;; *) continue ;; esac
  sed -i '1s|^#!.*|#!/usr/bin/env python3|' "$script"
done

sha256sum "$REQ_FILE" | awk '{print $1}' > "$VENV_DIR/.requirements.sha256"
touch "$VENV_DIR/.prebuilt"

# Strip build-time artifacts (saves ~5-10MB)
# 保留 dist-info — importlib.metadata.version() 运行时需要查包版本（如 werkzeug）
SITE_PKG="$VENV_DIR/lib/python${PYTHON_VER}/site-packages"
find "$SITE_PKG" -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
find "$SITE_PKG" -name "*.egg-info" -type d -exec rm -rf {} + 2>/dev/null || true
find "$SITE_PKG" -name "*.pyc" -delete 2>/dev/null || true
