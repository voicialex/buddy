#!/usr/bin/env bash
# ros2 CLI wrapper for buddy deb package
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# 检测已安装的 Python 包版本（而不是系统默认版本）
PYVER=""
for py in python3.10 python3.12 python3.11; do
  if [ -d "$DIR/lib/$py/site-packages/ros2cli" ]; then
    PYVER="$py"
    break
  fi
done

if [ -z "$PYVER" ]; then
  echo "Error: ros2cli not found in $DIR/lib/python3.*/site-packages" >&2
  exit 1
fi

export LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$DIR/lib/$PYVER/site-packages:$DIR/lib/$PYVER/dist-packages${PYTHONPATH:+:$PYTHONPATH}"
exec "/usr/bin/$PYVER" "$DIR/bin/_ros2_entry" "$@"
