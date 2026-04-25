#!/usr/bin/env bash
# build.sh — 编译 buddy_robot (underlay + overlay + 测试)
#
# 用法:
#   ./build.sh          # 编译 + 测试
#   ./build.sh -c       # 清理 overlay 后重新编译 + 测试
#   ./build.sh build    # 仅编译
#   ./build.sh test     # 仅测试
#   ./build.sh clean    # 仅清理 overlay
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ─── 颜色输出 ───
info()  { echo -e "\033[1;34m[INFO]\033[0m  $*"; }
ok()    { echo -e "\033[1;32m[OK]\033[0m    $*"; }
fail()  { echo -e "\033[1;31m[FAIL]\033[0m  $*"; exit 1; }

# ─── 检测 ROS2 发行版 ───
# shellcheck source=/dev/null
source /etc/os-release
case "$VERSION_ID" in
    22.04) ROS_DISTRO="humble" ;;
    24.04) ROS_DISTRO="jazzy" ;;
    *)     fail "不支持的 Ubuntu 版本: $VERSION_ID（需要 22.04 或 24.04）" ;;
esac

ARCH="$(uname -m)"
UNDERLAY="$PROJECT_ROOT/third_party/ros2/${ROS_DISTRO}/install"
OVERLAY="$PROJECT_ROOT/src/buddy_robot"

# ─── 清除残留 ROS 环境变量，防止 /opt/ros 干扰 ───
clean_env() {
    for var in AMENT_PREFIX_PATH CMAKE_PREFIX_PATH COLCON_PREFIX_PATH \
               PYTHONPATH LD_LIBRARY_PATH ROS_PACKAGE_PATH ROS_VERSION \
               ROS_DISTRO ROS_PYTHON_VERSION; do
        unset "$var" 2>/dev/null || true
    done
    export PATH="$(echo "$PATH" | tr ':' '\n' | grep -v '/opt/ros' | paste -sd:)"
}

source_setup() {
    # shellcheck source=/dev/null
    set +u; source "$1"; set -u
}

# ─── Underlay: 下载预编译 ros2_core ───
setup_underlay() {
    if [ -f "$UNDERLAY/setup.bash" ]; then
        ok "Underlay 已就绪: $UNDERLAY"
        return 0
    fi

    local version
    version="$(cat "$PROJECT_ROOT/.ros_core_version")"
    local tarball="ros2-${ROS_DISTRO}-${ARCH}.tar.gz"
    local url="https://github.com/voicialex/ros2_core/releases/download/${version}/${tarball}"

    local tmpfile
    tmpfile="$(mktemp)"
    trap "rm -f $tmpfile" EXIT

    info "下载 ${tarball} (${version})..."
    curl -fSL "$url" -o "$tmpfile"

    rm -rf "$UNDERLAY"
    mkdir -p "$UNDERLAY"
    tar xzf "$tmpfile" -C "$UNDERLAY"
    ok "Underlay 就绪: $UNDERLAY"
}

# ─── Overlay: 编译 buddy_robot ───
build_overlay() {
    [ -f "$UNDERLAY/setup.bash" ] || fail "Underlay 未就绪"
    source_setup "$UNDERLAY/setup.bash"
    cd "$OVERLAY"
    info "编译 overlay (buddy_robot)..."
    colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Release
    ok "编译完成"
}

# ─── 测试 ───
run_tests() {
    [ -f "$UNDERLAY/setup.bash" ] || fail "Underlay 未就绪"
    [ -f "$OVERLAY/install/setup.bash" ] || fail "未编译，请先运行: $0"
    source_setup "$UNDERLAY/setup.bash"
    source_setup "$OVERLAY/install/setup.bash"
    cd "$OVERLAY"
    colcon test --event-handlers console_cohesion+
    colcon test-result --verbose
    ok "测试完成"
}

# ─── 清理 ───
clean_overlay() {
    rm -rf "$OVERLAY/build" "$OVERLAY/install" "$OVERLAY/log"
    ok "Overlay 已清理"
}

# ─── 解析参数 ───
CLEAN=""
ACTION="all"

while [ $# -gt 0 ]; do
    case "$1" in
        -c|--clean) CLEAN=1; shift ;;
        build|test|clean) ACTION="$1"; shift ;;
        -h|--help)
            echo "用法: $0 [-c] [build|test|clean]"
            echo ""
            echo "  (默认)   setup + build + test"
            echo "  -c       清理 overlay 后重新编译 + 测试"
            echo "  build    仅编译"
            echo "  test     仅测试"
            echo "  clean    仅清理 overlay"
            exit 0 ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

info "Ubuntu $VERSION_ID → ROS2 ${ROS_DISTRO^^} (${ARCH})"
clean_env

if [ "$ACTION" = "clean" ]; then
    clean_overlay
    exit 0
fi

if [ -n "$CLEAN" ]; then
    clean_overlay
fi

setup_underlay

case "$ACTION" in
    all)   build_overlay; run_tests ;;
    build) build_overlay ;;
    test)  run_tests ;;
esac
