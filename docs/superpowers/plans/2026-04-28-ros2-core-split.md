# ros2_core 拆分实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `third_party/ros2/` 拆分为独立仓库 `ros2_core`，通过 GitHub Release 发布预编译 assets，buddy_robot 下载即用。

**Architecture:** ros2_core 仓库存储 repos 文件 + 锁定的源码，CI 在 tag 触发时编译并发布 tarball。buddy_robot 通过 `.ros_core_version` 锁定版本，`setup_underlay.sh` 下载解压，`build_all.sh` 简化为只编译 overlay。

**Tech Stack:** Bash, colcon, GitHub Actions, curl

**Spec:** `docs/superpowers/specs/2026-04-28-ros2-core-split-design.md`

---

## File Map

### ros2_core 仓库（新建，路径在仓库外）

| Action | Path | Responsibility |
|--------|------|---------------|
| Create | `repos/humble.repos` | Humble 分支仓库列表 |
| Create | `repos/jazzy.repos` | Jazzy 分支仓库列表 |
| Create | `scripts/update_src.sh` | 更新源码脚本 |
| Create | `scripts/build.sh` | 本地编译脚本 |
| Create | `.github/workflows/release.yml` | CI: tag → 编译 → 发布 |
| Create | `VERSION` | 当前版本号 |
| Create | `README.md` | 说明文档 |

### buddy_robot 仓库（当前项目）

| Action | Path | Responsibility |
|--------|------|---------------|
| Create | `.ros_core_version` | 锁定 ros2_core 版本 |
| Create | `scripts/setup_underlay.sh` | 下载预编译 underlay |
| Rewrite | `scripts/build_all.sh` | 简化为只编译 overlay |
| Modify | `.gitignore` | 移除 src/build/log 规则，保留 install |
| Delete | `third_party/ros2/humble/ros2_minimal.repos` | 移到 ros2_core |
| Delete | `third_party/ros2/humble/Dockerfile` | 移到 ros2_core |
| Delete | `third_party/ros2/humble/.gitkeep` | 不再需要 |
| Delete | `third_party/ros2/jazzy/ros2_minimal.repos` | 移到 ros2_core |
| Delete | `third_party/ros2/jazzy/ros2_minimal.lock.repos` | 源码锁定在 ros2_core |
| Delete | `third_party/ros2/jazzy/Dockerfile` | 移到 ros2_core |
| Delete | `third_party/ros2/jazzy/.gitkeep` | 不再需要 |
| Modify | `CLAUDE.md` | 更新构建说明 |
| Modify | `README.md` | 更新构建说明 |
| Modify | `docs/build_guide.md` | 大幅简化 |

---

## Task 1: 初始化 ros2_core 仓库

**Files:**
- Create: `~/ros2_core/` (独立仓库)

- [ ] **Step 1: Clone 并初始化 ros2_core 仓库**

```bash
cd ~
git clone git@github.com:voicialex/ros2_core.git
cd ros2_core
```

- [ ] **Step 2: 创建目录结构**

```bash
mkdir -p repos scripts src/humble src/jazzy .github/workflows
```

- [ ] **Step 3: 从 buddy_robot 复制 repos 文件**

```bash
cp /home/seb/buddy_robot/third_party/ros2/humble/ros2_minimal.repos repos/humble.repos
cp /home/seb/buddy_robot/third_party/ros2/jazzy/ros2_minimal.repos repos/jazzy.repos
```

- [ ] **Step 4: 创建 VERSION 文件**

```bash
echo "v2026.04.1" > VERSION
```

- [ ] **Step 5: 提交初始结构**

```bash
git add repos/ VERSION
git commit -m "feat: Add repos files and VERSION"
```

---

## Task 2: 创建 update_src.sh 脚本

**Files:**
- Create: `~/ros2_core/scripts/update_src.sh`

- [ ] **Step 1: 创建脚本**

```bash
cat > ~/ros2_core/scripts/update_src.sh << 'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail

DISTRO="${1:?用法: $0 humble|jazzy}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPOS_FILE="$REPO_ROOT/repos/${DISTRO}.repos"
SRC_DIR="$REPO_ROOT/src/$DISTRO"

if [ ! -f "$REPOS_FILE" ]; then
    echo "repos 文件不存在: $REPOS_FILE"
    exit 1
fi

# 安装 vcstool
if ! command -v vcs &>/dev/null; then
    pip3 install --break-system-packages vcstool
fi

# 拉取/更新源码
mkdir -p "$SRC_DIR"
echo "[INFO] 拉取 $DISTRO 源码..."
vcs import "$SRC_DIR" < "$REPOS_FILE"

# 删除 .git 目录（不需要子仓库信息）
find "$SRC_DIR" -name ".git" -type d -exec rm -rf {} +

COUNT=$(ls "$SRC_DIR" | wc -l)
echo "[OK] 已拉取 $COUNT 个仓库到 src/$DISTRO"
echo "请 git add src/$DISTRO/ && git commit"
SCRIPT
chmod +x ~/ros2_core/scripts/update_src.sh
```

- [ ] **Step 2: 执行 update_src.sh 拉取 jazzy 源码**

```bash
cd ~/ros2_core
./scripts/update_src.sh jazzy
```

注意：这会拉取约 85MB 源码，需要几分钟。

- [ ] **Step 3: 提交 jazzy 源码**

```bash
cd ~/ros2_core
git add src/jazzy/
git commit -m "feat: Add jazzy source (31 repos, locked)"
```

- [ ] **Step 4: 拉取并提交 humble 源码**

```bash
cd ~/ros2_core
./scripts/update_src.sh humble
git add src/humble/
git commit -m "feat: Add humble source (31 repos, locked)"
```

- [ ] **Step 5: 提交脚本**

```bash
cd ~/ros2_core
git add scripts/update_src.sh
git commit -m "feat: Add update_src.sh script"
```

---

## Task 3: 创建 CI workflow

**Files:**
- Create: `~/ros2_core/.github/workflows/release.yml`

- [ ] **Step 1: 创建 release.yml**

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

- [ ] **Step 2: 提交 CI 配置**

```bash
cd ~/ros2_core
git add .github/workflows/release.yml
git commit -m "feat: Add CI release workflow"
```

- [ ] **Step 3: 创建 README.md**

```bash
cat > ~/ros2_core/README.md << 'EOF'
# ros2_core

Pre-built ROS2 core libraries for buddy_robot.

## Releases

Download from [GitHub Releases](https://github.com/voicialex/ros2_core/releases).

Assets:
- `ros2-humble-x86_64.tar.gz` — ROS2 Humble (Ubuntu 22.04)
- `ros2-jazzy-x86_64.tar.gz` — ROS2 Jazzy (Ubuntu 24.04)

## Update source

```bash
./scripts/update_src.sh jazzy    # or humble
git add src/jazzy/ && git commit
git tag vYYYY.MM.N && git push --tags
```

CI will build and publish automatically.
EOF
git add README.md
git commit -m "feat: Add README"
```

- [ ] **Step 4: 推送并触发首次 CI**

```bash
cd ~/ros2_core
git tag v2026.04.1
git push -u origin main
git push --tags
```

等待 CI 完成后确认 Release 页面有两个 tarball。

---

## Task 4: buddy_robot — 创建下载脚本和版本锁定

**Files:**
- Create: `/home/seb/buddy_robot/.ros_core_version`
- Create: `/home/seb/buddy_robot/scripts/setup_underlay.sh`

- [ ] **Step 1: 创建 .ros_core_version**

```bash
echo "v2026.04.1" > /home/seb/buddy_robot/.ros_core_version
```

- [ ] **Step 2: 创建 setup_underlay.sh**

文件内容来自 spec 第 3.1 节，完整代码：

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

source /etc/os-release
case "$VERSION_ID" in
    22.04) ROS_DISTRO="humble" ;;
    24.04) ROS_DISTRO="jazzy" ;;
    *)     echo "不支持的 Ubuntu 版本: $VERSION_ID"; exit 1 ;;
esac
ARCH="$(uname -m)"

VERSION="${1:-$(cat "$PROJECT_ROOT/.ros_core_version")}"
TARBALL="ros2-${ROS_DISTRO}-${ARCH}.tar.gz"
URL="https://github.com/voicialex/ros2_core/releases/download/${VERSION}/${TARBALL}"

TMPFILE="$(mktemp)"
trap "rm -f $TMPFILE" EXIT
echo "[INFO] 下载 ${TARBALL} (${VERSION})..."
curl -fSL "$URL" -o "$TMPFILE"

INSTALL_DIR="$PROJECT_ROOT/third_party/ros2/${ROS_DISTRO}/install"
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
tar xzf "$TMPFILE" -C "$INSTALL_DIR"
echo "[OK] Underlay 就绪: ${INSTALL_DIR}"
```

```bash
chmod +x /home/seb/buddy_robot/scripts/setup_underlay.sh
```

- [ ] **Step 3: 验证下载脚本**

前提：Task 3 的 CI 已完成并发布了 Release。

```bash
bash /home/seb/buddy_robot/scripts/setup_underlay.sh
ls /home/seb/buddy_robot/third_party/ros2/jazzy/install/setup.bash
```

Expected: `setup.bash` 文件存在。

---

## Task 5: buddy_robot — 简化 build_all.sh

**Files:**
- Rewrite: `/home/seb/buddy_robot/scripts/build_all.sh`

- [ ] **Step 1: 重写 build_all.sh**

完整替换为 spec 第 3.2 节的简化版：

```bash
#!/usr/bin/env bash
# build_all.sh — Overlay 构建脚本
#
# 用法:
#   ./scripts/build_all.sh setup   # 下载预编译 underlay
#   ./scripts/build_all.sh build   # 编译 overlay (buddy_robot)
#   ./scripts/build_all.sh test    # 运行测试
#   ./scripts/build_all.sh all     # setup + build + test
#   ./scripts/build_all.sh clean   # 清理 overlay 构建产物

set -euo pipefail

# ─── 颜色输出 ───
info()  { echo -e "\033[1;34m[INFO]\033[0m  $*"; }
ok()    { echo -e "\033[1;32m[OK]\033[0m    $*"; }
fail()  { echo -e "\033[1;31m[FAIL]\033[0m  $*"; exit 1; }

# ─── 根据 Ubuntu 版本自动选择 ROS2 发行版 ───
# shellcheck source=/dev/null
source /etc/os-release
case "$VERSION_ID" in
    22.04) ROS_DISTRO="humble" ;;
    24.04) ROS_DISTRO="jazzy" ;;
    *)     fail "不支持的 Ubuntu 版本: $VERSION_ID（需要 22.04 或 24.04）" ;;
esac
info "检测到 Ubuntu $VERSION_ID → ROS2 ${ROS_DISTRO^^}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
UNDERLAY="$PROJECT_ROOT/third_party/ros2/${ROS_DISTRO}/install"
OVERLAY="$PROJECT_ROOT/src/buddy_robot"

source_setup() {
    # shellcheck source=/dev/null
    set +u; source "$1"; set -u
}

# ─── 入口 ───
case "${1:-all}" in
    setup)
        bash "$SCRIPT_DIR/setup_underlay.sh"
        ;;
    build)
        [ -f "$UNDERLAY/setup.bash" ] || fail "Underlay 未下载。请先运行: $0 setup"
        source_setup "$UNDERLAY/setup.bash"
        cd "$OVERLAY"
        info "编译 overlay (buddy_robot)..."
        colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Release
        ok "Overlay 编译完成"
        ;;
    test)
        [ -f "$UNDERLAY/setup.bash" ] || fail "Underlay 未下载"
        [ -f "$OVERLAY/install/setup.bash" ]  || fail "Overlay 未编译。请先运行: $0 build"
        source_setup "$UNDERLAY/setup.bash"
        source_setup "$OVERLAY/install/setup.bash"
        cd "$OVERLAY"
        colcon test --event-handlers console_cohesion+
        colcon test-result --verbose
        ok "测试完成"
        ;;
    all)
        "$0" setup
        "$0" build
        "$0" test
        ;;
    clean)
        rm -rf "$OVERLAY/build" "$OVERLAY/install" "$OVERLAY/log"
        ok "Overlay 构建产物已清理"
        ;;
    *)
        echo "用法: $0 {setup|build|test|all|clean}"
        exit 1
        ;;
esac
```

- [ ] **Step 2: 验证构建和测试**

```bash
bash /home/seb/buddy_robot/scripts/build_all.sh build
bash /home/seb/buddy_robot/scripts/build_all.sh test
```

Expected: 8 packages built, 26 tests passed.

---

## Task 6: buddy_robot — 清理 third_party 和更新 .gitignore

**Files:**
- Modify: `/home/seb/buddy_robot/.gitignore`
- Delete: 6 files under `third_party/ros2/`

- [ ] **Step 1: 删除已迁移到 ros2_core 的文件**

```bash
cd /home/seb/buddy_robot
git rm third_party/ros2/humble/ros2_minimal.repos
git rm third_party/ros2/humble/Dockerfile
git rm third_party/ros2/humble/.gitkeep
git rm third_party/ros2/jazzy/ros2_minimal.repos
git rm third_party/ros2/jazzy/ros2_minimal.lock.repos
git rm third_party/ros2/jazzy/Dockerfile
git rm third_party/ros2/jazzy/.gitkeep
```

- [ ] **Step 2: 更新 .gitignore**

完整替换为：

```
models/
logs/
build/
install/
log/
build.log
*.rknn
*.onnx
*.pyc
__pycache__/
# third_party: only keep downloaded underlay assets
third_party/ros2/humble/install/
third_party/ros2/jazzy/install/
# workspace build artifacts
src/buddy_robot/build/
src/buddy_robot/install/
src/buddy_robot/log/
```

- [ ] **Step 3: 删除 package_list.txt（不再需要）**

```bash
git rm scripts/package_list.txt
```

---

## Task 7: buddy_robot — 更新文档

**Files:**
- Modify: `/home/seb/buddy_robot/CLAUDE.md`
- Modify: `/home/seb/buddy_robot/README.md`
- Modify: `/home/seb/buddy_robot/docs/build_guide.md`

- [ ] **Step 1: 更新 CLAUDE.md**

主要改动：
- Build Commands 部分：移除 underlay/overlay 分步，改为 `setup` + `build`
- Build Layout 部分：改为 "Underlay 从 GitHub Release 下载，版本锁定在 `.ros_core_version`"
- 删除 `third_party/ros2/*/ros2_minimal.repos` 的 Key Files 引用
- 添加 `.ros_core_version` 和 `scripts/setup_underlay.sh` 到 Key Files

- [ ] **Step 2: 更新 README.md**

主要改动：
- 前置条件：移除 vcs/rosdep，保留 colcon
- 构建部分：改为 `setup` + `build` + `test`
- 删除手动构建（Underlay）部分
- 产物目录表：移除 third_party 源码/编译相关行
- 项目结构：移除 third_party 下的 repos/Dockerfile

- [ ] **Step 3: 大幅简化 docs/build_guide.md**

移除：
- 第 2 节（三个工具的作用）— 只保留 colcon
- 第 3 节（两个 repos 文件）— 不再需要
- 第 4 节中 Underlay 编译部分
- 第 8 节（交叉编译 Docker）— 不再需要

保留并更新：
- 第 1 节：架构图简化为 "下载 underlay → 编译 overlay"
- 第 5-7 节：简化运行、常见操作、换台机器说明

---

## Task 8: 提交并验证

- [ ] **Step 1: 提交所有改动**

```bash
cd /home/seb/buddy_robot
git add .ros_core_version scripts/ .gitignore
git add CLAUDE.md README.md docs/
git commit -m "feat(build): [PRO-10000] Switch to pre-built ros2_core assets"
```

- [ ] **Step 2: 端到端验证**

从干净状态验证完整流程：

```bash
# 模拟干净环境
rm -rf third_party/ros2/jazzy/install
rm -rf src/buddy_robot/build src/buddy_robot/install src/buddy_robot/log

# 完整流程
bash scripts/build_all.sh all
```

Expected:
1. setup: 下载 tarball + 解压
2. build: 8 packages finished
3. test: 26 tests passed

- [ ] **Step 3: 推送**

```bash
git push
```
