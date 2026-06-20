#!/bin/bash
# setup.bash - Source this file to use ROS 2 commands
# Usage: source /opt/buddy/setup.bash

_BUDDY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Detect Python version
_PYVER=""
for py in python3.10 python3.12 python3.11; do
  if [ -d "$_BUDDY_DIR/lib/$py/site-packages/ros2cli" ]; then
    _PYVER="$py"
    break
  fi
done

if [ -z "$_PYVER" ]; then
  echo "Error: ros2cli not found in $_BUDDY_DIR/lib/python3.*/site-packages" >&2
  return 1
fi

# Environment variables
export PATH="$_BUDDY_DIR/bin${PATH:+:$PATH}"
export LD_LIBRARY_PATH="$_BUDDY_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$_BUDDY_DIR/lib/$_PYVER/site-packages:$_BUDDY_DIR/lib/$_PYVER/dist-packages${PYTHONPATH:+:$PYTHONPATH}"
export AMENT_PREFIX_PATH="$_BUDDY_DIR${AMENT_PREFIX_PATH:+:$AMENT_PREFIX_PATH}"
export COLCON_PREFIX_PATH="$_BUDDY_DIR${COLCON_PREFIX_PATH:+:$COLCON_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="$_BUDDY_DIR/share${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"

# ROS 2 specific
export ROS_DISTRO="${BUDDY_ROS2_DISTRO:-humble}"
export ROS_VERSION=2
export ROS_PYTHON_VERSION=3

# Cleanup
unset _BUDDY_DIR _PYVER
