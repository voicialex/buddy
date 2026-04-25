#!/usr/bin/env bash
# run.sh — 启动 buddy_robot（不需要 ros2 launch）
#
# 用法:
#   ./run.sh              # 启动所有节点
#   ./run.sh audio        # 仅启动 audio 节点
#   ./run.sh stop         # 停止所有节点
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ─── 检测 ROS2 发行版 ───
# shellcheck source=/dev/null
source /etc/os-release
case "$VERSION_ID" in
    22.04) ROS_DISTRO="humble" ;;
    24.04) ROS_DISTRO="jazzy" ;;
    *)     echo "不支持的 Ubuntu 版本: $VERSION_ID"; exit 1 ;;
esac

UNDERLAY="$PROJECT_ROOT/third_party/ros2/${ROS_DISTRO}/install"
OVERLAY="$PROJECT_ROOT/src/buddy_robot"
PARAMS="$OVERLAY/install/buddy_bringup/share/buddy_bringup/params/buddy_params.yaml"

[ -f "$UNDERLAY/setup.bash" ] || { echo "Underlay 未就绪，请先运行: ./build.sh"; exit 1; }
[ -f "$OVERLAY/install/setup.bash" ] || { echo "未编译，请先运行: ./build.sh"; exit 1; }

# 清除残留环境
for var in AMENT_PREFIX_PATH CMAKE_PREFIX_PATH PYTHONPATH LD_LIBRARY_PATH \
           ROS_PACKAGE_PATH ROS_VERSION ROS_DISTRO ROS_PYTHON_VERSION; do
    unset "$var" 2>/dev/null || true
done
export PATH="$(echo "$PATH" | tr ':' '\n' | grep -v '/opt/ros' | paste -sd:)"

# shellcheck source=/dev/null
set +u; source "$UNDERLAY/setup.bash"; set -u
# shellcheck source=/dev/null
set +u; source "$OVERLAY/install/setup.bash"; set -u

NODES=(audio_node vision_node cloud_node state_machine_node dialog_node sentence_node)
BUDDY_APP="$OVERLAY/install/buddy_app/lib/buddy_app/buddy_app"

stop_all() {
    pkill -f buddy_app 2>/dev/null || true
    for node in "${NODES[@]}"; do
        pkill -f "$node" 2>/dev/null || true
    done
}

start_app() {
    echo "[INFO] 启动 buddy_app 单进程 (Ctrl+C 停止)..."
    "$BUDDY_APP" --ros-args --params-file "$PARAMS"
}

start_one() {
    local node="$1"
    local pkg="buddy_${node%_node}"
    "$OVERLAY/install/$pkg/lib/$pkg/$node" --ros-args --params-file "$PARAMS"
}

case "${1:-}" in
    stop)  stop_all ;;
    audio|vision|cloud|state_machine|dialog|sentence)
           start_one "${1}_node" ;;
    *)     start_app ;;
esac
