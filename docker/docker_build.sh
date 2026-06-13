#!/usr/bin/env bash
# docker_build.sh — 在 Docker 容器中构建 buddy (x86_64 native / arm64 cross)
# 零外部镜像依赖，只需 Docker + 预编译依赖即可构建
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DOCKERFILE="$SCRIPT_DIR/Dockerfile"

# ─── 函数定义 ───

usage() {
    cat <<'EOF'
用法: ./docker/docker_build.sh [选项]

在 Docker 容器中构建 buddy，生成 .deb 安装包和 models 压缩包。

选项:
  -a, --arch ARCH        目标架构: x86_64 (默认) 或 arm64
  -d, --device DEV       设备类型: cpu (默认), gpu (CUDA), npu (RKNN, arm64 only)
  -v, --version VER      deb 版本号 (默认: 1.0.0)
  -c, --clean            清除编译缓存后全量重编
  --no-cache             强制重建 Docker 缓存
  -h, --help             显示帮助

示例:
  ./docker/docker_build.sh                     # x86_64 + cpu
  ./docker/docker_build.sh -d gpu              # x86_64 + GPU (CUDA)
  ./docker/docker_build.sh -a arm64 -d npu    # arm64 + NPU (交叉编译)
  ./docker/docker_build.sh -c                  # 清除缓存重编
EOF
}

die() {
    echo "[ERROR] $1" >&2
    [[ -n "${2:-}" ]] && echo "[HINT] $2" >&2
    exit 1
}

parse_args() {
    TARGET_ARCH="x86_64"
    DEVICE="cpu"
    VERSION="1.0.0"
    CLEAN=false
    NO_CACHE=""

    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help) usage; exit 0 ;;
            -a|--arch) TARGET_ARCH="$2"; shift 2 ;;
            -d|--device) DEVICE="$2"; shift 2 ;;
            -v|--version) VERSION="$2"; shift 2 ;;
            -c|--clean) CLEAN=true; shift ;;
            --no-cache) NO_CACHE="--no-cache"; shift ;;
            arm64|aarch64) TARGET_ARCH="arm64"; shift ;;
            x86_64|x86|amd64) TARGET_ARCH="x86_64"; shift ;;
            *) echo "未知参数: $1"; usage; exit 1 ;;
        esac
    done

    case "$TARGET_ARCH" in
        arm64|aarch64) TARGET_ARCH="arm64" ;;
        x86_64|x86|amd64) TARGET_ARCH="x86_64" ;;
        *) die "不支持的架构: $TARGET_ARCH" ;;
    esac

    case "$DEVICE" in
        cpu|gpu|npu) ;;
        *) die "不支持的设备类型: $DEVICE (支持: cpu, gpu, npu)" ;;
    esac

    if [[ "$DEVICE" == "npu" && "$TARGET_ARCH" != "arm64" ]]; then
        die "NPU (RKNN) 仅支持 arm64 架构"
    fi
}

preflight_check() {
    if ! command -v docker &>/dev/null; then
        die "Docker 未安装 — https://docs.docker.com/engine/install/"
    fi
    if ! docker info &>/dev/null; then
        die "Docker 服务未启动或当前用户无权限" \
            "sudo systemctl start docker && sudo usermod -aG docker \$USER"
    fi
}

# 检查预编译依赖
check_prebuilt() {
    local prebuilt_dir="$REPO_ROOT/prebuilt/$TARGET_ARCH"
    if [[ "$TARGET_ARCH" == "arm64" ]]; then
        prebuilt_dir="$REPO_ROOT/prebuilt/aarch64"
    fi

    local missing=()
    [[ -d "$prebuilt_dir/ros2_core" ]] || missing+=("ros2_core")
    [[ -d "$prebuilt_dir/onnxruntime" ]] || missing+=("onnxruntime")
    [[ -d "$prebuilt_dir/sherpa-onnx" ]] || missing+=("sherpa-onnx")
    [[ -d "$prebuilt_dir/opencv" ]] || missing+=("opencv")

    if [[ ${#missing[@]} -gt 0 ]]; then
        die "缺少预编译依赖: ${missing[*]}" \
            "运行: ./scripts/setup_prebuilt.sh --arch $TARGET_ARCH"
    fi
}

do_build() {
    local prebuilt_dir="$TARGET_ARCH"
    local deb_suffix="amd64"
    local output_dir="$REPO_ROOT/output/x86_64/deb"

    if [[ "$TARGET_ARCH" == "arm64" ]]; then
        prebuilt_dir="aarch64"
        deb_suffix="arm64"
        output_dir="$REPO_ROOT/output/aarch64/deb"
    fi

    local deb_file="$output_dir/buddy-robot_${VERSION}_${deb_suffix}_${DEVICE}.deb"
    local models_file="$output_dir/buddy-models_${VERSION}_${DEVICE}.tar.gz"
    local parallel="${BUDDY_PARALLEL_WORKERS:-$(nproc)}"

    if [[ "$CLEAN" == true ]]; then
        echo "[CLEAN] 清除 $output_dir"
        rm -rf "$output_dir"
    fi
    mkdir -p "$output_dir"

    local cache_flag=()
    [[ "$CLEAN" == true ]] && cache_flag=(--no-cache)
    cache_flag+=($NO_CACHE)

    echo "=========================================="
    echo " Docker 构建 buddy (arch: $TARGET_ARCH, device: $DEVICE)"
    echo "=========================================="

    local start_time
    start_time=$(date +%s)

    # 构建 .deb
    echo "[INFO] 构建 buddy-robot_${VERSION}_${deb_suffix}_${DEVICE}.deb ..."
    DOCKER_BUILDKIT=1 docker build \
        "${cache_flag[@]}" \
        --build-arg VERSION="$VERSION" \
        --build-arg PARALLEL_WORKERS="$parallel" \
        --build-arg DEVICE="$DEVICE" \
        --build-arg TARGET_ARCH="$TARGET_ARCH" \
        --target export-package \
        --output "type=local,dest=$output_dir/" \
        -f "$DOCKERFILE" \
        "$REPO_ROOT"

    # 构建 models (仅当不存在时)
    if [[ ! -f "$models_file" ]]; then
        echo "[INFO] 构建 models 压缩包..."
        DOCKER_BUILDKIT=1 docker build \
            "${cache_flag[@]}" \
            --build-arg VERSION="$VERSION" \
            --build-arg DEVICE="$DEVICE" \
            --build-arg TARGET_ARCH="$TARGET_ARCH" \
            --target export-models \
            --output "type=local,dest=$output_dir/" \
            -f "$DOCKERFILE" \
            "$REPO_ROOT"
    else
        echo "[INFO] 复用已有 models: $(basename "$models_file")"
    fi

    local elapsed
    elapsed=$(( $(date +%s) - start_time ))
    printf "[OK] 总耗时: %d分%d秒\n" $((elapsed/60)) $((elapsed%60))
    echo ""
    [[ -f "$deb_file" ]] && echo "[OK] Package: $deb_file ($(du -sh "$deb_file" | cut -f1))"
    [[ -f "$models_file" ]] && echo "[OK] Models:  $models_file ($(du -sh "$models_file" | cut -f1))"
}

# ─── 主流程 ───

parse_args "$@"
preflight_check
check_prebuilt
do_build
