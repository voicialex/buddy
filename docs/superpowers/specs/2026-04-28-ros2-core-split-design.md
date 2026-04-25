# ros2_core 拆分设计

将 `third_party/ros2/` 拆分为独立仓库 `ros2_core`，通过 GitHub Release 发布预编译 assets，buddy_robot 下载即用。

## 1. 动机

| | 当前：源码编译 | 拆分后：下载 assets |
|--|--------------|-------------------|
| 首次构建 | ~2 分钟（编译 80 包） | ~10 秒（下载 55MB） |
| 仓库体积 | third_party 占 170MB 源码 | 不含源码，只有 ~2.6MB 业务代码 |
| 换台机器 | 拉源码 → 装依赖 → 编译 | 下载 tarball → 解压 |
| CI | 每次编译 underlay | 下载 assets，秒级 |
| RK3588 部署 | 板端装编译工具链 | 下载 → 解压 → 运行 |

## 2. ros2_core 仓库

### 2.1 仓库结构

```text
ros2_core/
├── .github/
│   └── workflows/
│       └── release.yml          # CI: tag 触发编译 + 发布
├── repos/
│   ├── humble.repos             # Humble 分支仓库列表（31 个）
│   └── jazzy.repos              # Jazzy 分支仓库列表（31 个）
├── src/
│   ├── humble/                  # Humble 源码（锁定提交，无 .git 目录，~85MB）
│   │   ├── ros2/rclcpp/
│   │   ├── ros2/rcl/
│   │   └── ...                  # 31 个仓库
│   └── jazzy/                   # Jazzy 源码（锁定提交，无 .git 目录，~85MB）
│       ├── ros2/rclcpp/
│       └── ...
├── scripts/
│   ├── build.sh                 # 本地编译脚本（CI 也用这个）
│   └── update_src.sh            # 更新源码：vcs import → 删 .git → 提交
├── VERSION                      # 当前版本号
└── README.md
```

仓库地址：`git@github.com:voicialex/ros2_core.git`

### 2.2 源码锁定策略

源码直接提交到 git 仓库，CI 不需要每次拉取。好处：

- CI 无网络依赖，直接 `colcon build`
- 源码版本由 git 提交历史锁定
- 不需要额外的 lock.repos 文件

更新源码流程（手动，极少执行）：

```bash
./scripts/update_src.sh jazzy
# → vcs import src/jazzy/ < repos/jazzy.repos
# → 删除 src/jazzy/ 下所有 .git 目录
# → git add + commit + tag + push
```

### 2.3 版本号规则

格式：`vYYYY.MM.N`

```text
v2026.04.1   → 2026 年 4 月第 1 次发布
v2026.04.2   → 同月修复/更新
v2026.10.1   → 升级 ROS2 依赖时
```

### 2.4 CI 设计

触发条件：`git tag v*`

```yaml
name: Build & Release
on:
  push:
    tags: ['v*']

jobs:
  build:
    strategy:
      matrix:
        include:
          - distro: humble
            runner: ubuntu-22.04
          - distro: jazzy
            runner: ubuntu-24.04
    runs-on: ${{ matrix.runner }}
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update && sudo apt-get install -y \
            python3-colcon-common-extensions python3-rosdep \
            libssl-dev libtinyxml2-dev
          sudo rosdep init 2>/dev/null || true
          rosdep update

      - name: Build ROS2 ${{ matrix.distro }}
        run: |
          cd src/${{ matrix.distro }}
          rosdep install --from-paths . \
            --ignore-src -y --skip-keys "python3" || true
          colcon build \
            --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
            --packages-up-to \
              rclcpp_lifecycle std_msgs sensor_msgs \
              builtin_interfaces rosidl_default_generators

      - name: Package
        run: |
          ARCH=$(uname -m)
          cd src/${{ matrix.distro }}
          tar czf ../../ros2-${{ matrix.distro }}-${ARCH}.tar.gz -C install .

      - name: Release
        uses: softprops/action-gh-release@v2
        with:
          files: ros2-${{ matrix.distro }}-*.tar.gz
```

说明：

- humble 和 jazzy 并行编译（matrix），总时间约 3-5 分钟
- 源码已在仓库里，CI 只做编译 + 打包 + 上传
- aarch64 后续补充：self-hosted runner 或 QEMU

### 2.5 Release 产物

```text
Release v2026.04.1
├── ros2-humble-x86_64.tar.gz    (~55MB)
├── ros2-jazzy-x86_64.tar.gz     (~55MB)
├── ros2-humble-aarch64.tar.gz   (后续补充)
└── ros2-jazzy-aarch64.tar.gz    (后续补充)
```

## 3. buddy_robot 侧改动

### 3.1 新增文件

**`.ros_core_version`** — 锁定依赖版本（和 `package-lock.json` 同性质）

```text
v2026.04.1
```

**`scripts/setup_underlay.sh`** — 下载预编译 underlay

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 检测 OS → ROS_DISTRO
source /etc/os-release
case "$VERSION_ID" in
    22.04) ROS_DISTRO="humble" ;;
    24.04) ROS_DISTRO="jazzy" ;;
    *)     echo "不支持的 Ubuntu 版本: $VERSION_ID"; exit 1 ;;
esac
ARCH="$(uname -m)"

# 读版本号
VERSION="${1:-$(cat "$PROJECT_ROOT/.ros_core_version")}"
TARBALL="ros2-${ROS_DISTRO}-${ARCH}.tar.gz"
URL="https://github.com/voicialex/ros2_core/releases/download/${VERSION}/${TARBALL}"

# 下载
TMPFILE="$(mktemp)"
trap "rm -f $TMPFILE" EXIT
echo "[INFO] 下载 ${TARBALL} (${VERSION})..."
curl -fSL "$URL" -o "$TMPFILE"

# 清理旧目录 + 解压
INSTALL_DIR="$PROJECT_ROOT/third_party/ros2/${ROS_DISTRO}/install"
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
tar xzf "$TMPFILE" -C "$INSTALL_DIR"
echo "[OK] Underlay 就绪: ${INSTALL_DIR}"
```

### 3.2 简化 `scripts/build_all.sh`

从 ~215 行简化到 ~40 行，移除所有 underlay 编译逻辑：

```bash
#!/usr/bin/env bash
set -euo pipefail

source /etc/os-release
case "$VERSION_ID" in
    22.04) ROS_DISTRO="humble" ;;
    24.04) ROS_DISTRO="jazzy" ;;
esac
echo "[INFO] Ubuntu $VERSION_ID → ROS2 ${ROS_DISTRO^^}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
UNDERLAY="$PROJECT_ROOT/third_party/ros2/${ROS_DISTRO}/install"
OVERLAY="$PROJECT_ROOT/src/buddy_robot"

source_setup() { set +u; source "$1"; set -u; }

case "${1:-all}" in
    setup)
        bash "$SCRIPT_DIR/setup_underlay.sh"
        ;;
    build)
        [ -f "$UNDERLAY/setup.bash" ] || { echo "先运行: $0 setup"; exit 1; }
        source_setup "$UNDERLAY/setup.bash"
        cd "$OVERLAY"
        colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Release
        ;;
    test)
        source_setup "$UNDERLAY/setup.bash"
        source_setup "$OVERLAY/install/setup.bash"
        cd "$OVERLAY"
        colcon test --event-handlers console_cohesion+
        colcon test-result --verbose
        ;;
    all)
        "$0" setup
        "$0" build
        "$0" test
        ;;
    clean)
        rm -rf "$OVERLAY/build" "$OVERLAY/install" "$OVERLAY/log"
        ;;
    *)
        echo "用法: $0 {setup|build|test|all|clean}"
        ;;
esac
```

### 3.3 删除的文件

| 删除 | 原因 |
|------|------|
| `third_party/ros2/humble/ros2_minimal.repos` | 移到 ros2_core |
| `third_party/ros2/humble/Dockerfile` | 移到 ros2_core |
| `third_party/ros2/jazzy/ros2_minimal.repos` | 移到 ros2_core |
| `third_party/ros2/jazzy/ros2_minimal.lock.repos` | 源码锁定在 ros2_core git 历史里 |
| `third_party/ros2/jazzy/Dockerfile` | 移到 ros2_core |
| `scripts/build_all.sh` 里 underlay 编译逻辑 | 不再本地编译 |

### 3.4 .gitignore 变化

```text
# 移除（不再有 src/build/log）
third_party/ros2/humble/src/
third_party/ros2/humble/build/
third_party/ros2/humble/log/
third_party/ros2/jazzy/src/
third_party/ros2/jazzy/build/
third_party/ros2/jazzy/log/

# 保留（下载的预编译产物，不提交到 buddy_robot 仓库）
# 版本锁定通过 .ros_core_version 文件
third_party/ros2/humble/install/
third_party/ros2/jazzy/install/
```

### 3.5 文档更新

- `README.md` — 构建部分改为"setup + build"
- `CLAUDE.md` — 同步更新
- `docs/build_guide.md` — 大幅简化，移除 underlay 编译内容
- `docs/architecture.md` — 更新依赖关系描述

## 4. 升级依赖流程

```bash
# ros2_core 侧
cd ros2_core
./scripts/update_src.sh jazzy           # 拉取最新源码
git add src/jazzy/
git commit -m "Update jazzy source"
git tag v2026.10.1
git push && git push --tags             # → CI 编译 + 发布

# buddy_robot 侧
echo "v2026.10.1" > .ros_core_version   # 改版本号
./scripts/build_all.sh setup            # 重新下载
./scripts/build_all.sh build            # 重新编译 overlay
```

## 5. 迁移步骤

1. 创建 ros2_core 仓库，迁移 repos + 源码 + CI
2. 打 tag 触发首次 CI 编译，发布 v2026.04.1
3. buddy_robot 删除 `third_party/ros2/` 下的 repos/Dockerfile/lock
4. 新增 `.ros_core_version`、`scripts/setup_underlay.sh`
5. 简化 `scripts/build_all.sh`
6. 更新文档
7. 验证：全新环境下 `./scripts/build_all.sh all` 能否跑通
