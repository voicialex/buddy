#!/usr/bin/env bash
# Prebuilt 依赖 + Buddy 模型 一键安装脚本
#
# 用法:
#   ./scripts/setup_prebuilt.sh                    # 安装全部 (host arch)
#   ./scripts/setup_prebuilt.sh --arch arm64 prebuilt  # 下载 arm64 预编译包到 prebuilt/aarch64/
#   ./scripts/setup_prebuilt.sh prebuilt           # 仅安装 prebuilt 库（ros2, onnxruntime, sherpa-onnx）
#   ./scripts/setup_prebuilt.sh models             # 下载所有模型 (ASR, KWS, TTS, FunASR, MOSS-TTS, Vision)
#
# 跳过逻辑: 已解压的目录不会重复处理
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# ══════════════════════════════════════════════════════════
# 参数解析 + 架构检测
# ══════════════════════════════════════════════════════════

ARCH="${BUDDY_ARCH:-$(uname -m)}"
PROXY_URL="${BUDDY_PROXY_URL:-}"
WITH_GPU_ORT=false

print_help() {
    local clash_port=""
    clash_port="$(ss -lntp 2>/dev/null | awk '/clash|verge/ && /127.0.0.1:/ { split($4,a,":"); print a[length(a)]; exit }')"

    cat <<EOF
Usage: ./scripts/setup_prebuilt.sh [OPTIONS]

Download prebuilt libraries and AI models for buddy.
Skips anything already present — safe to re-run.

Options:
  --arch <x86_64|arm64>   Target arch for prebuilt libs (default: host)
  --proxy <url>           HTTP proxy for this run
  --with-gpu-ort          Include onnxruntime-gpu (x86_64 only)
  -h, --help              Show this help
EOF

    if [[ -n "$clash_port" ]]; then
        echo ""
        echo "  Detected proxy: http://127.0.0.1:${clash_port}"
        echo "  Use: --proxy http://127.0.0.1:${clash_port}"
    fi

    cat <<'EOF'

Models (auto-downloaded, skipped if present):
  Auto     ASR (ONNX x86 / RKNN arm64), KWS, TTS (kokoro + melo), FunASR,
           MOSS-TTS, Vision (face_emotion)
  Manual   ChatTTS (~1.2GB, auto on first run), Ollama (~4.4GB), RKLLM (~3.5GB)

Override HF source via env vars:
  BUDDY_ZIPFORMER_RKNN_HF_BASE  BUDDY_ZIPFORMER_ONNX_HF_BASE
  BUDDY_MELO_TTS_HF_BASE        BUDDY_FACE_EMOTION_HF_BASE

Examples:
  ./scripts/setup_prebuilt.sh                              # Everything (host)
  ./scripts/setup_prebuilt.sh --arch arm64                 # Cross-compile
  ./scripts/setup_prebuilt.sh --proxy http://127.0.0.1:7890
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            print_help
            exit 0
            ;;
        --arch) ARCH="$2"; shift 2 ;;
        --proxy) PROXY_URL="$2"; shift 2 ;;
        --with-gpu-ort) WITH_GPU_ORT=true; shift ;;
        *)
            echo "[ERROR] Unknown option: $1"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
    esac
done

case "$ARCH" in
    x86_64|amd64) ARCH_ORT="x64"; ARCH_SHERPA="x64"; ARCH_NORMALIZED="x86_64" ;;
    aarch64|arm64) ARCH_ORT="aarch64"; ARCH_SHERPA="aarch64"; ARCH_NORMALIZED="aarch64" ;;
    *) echo "[ERROR] Unsupported arch: $ARCH"; exit 1 ;;
esac

PREBUILT_DIR="$PROJECT_DIR/prebuilt/${ARCH_NORMALIZED}"
MODELS_DIR="$PROJECT_DIR/models"

cd "$PROJECT_DIR"

# ══════════════════════════════════════════════════════════
# 可配置变量
# ══════════════════════════════════════════════════════════

# ROS 2 tarball
ROS2_CORE_BASE="$PROJECT_DIR/../ros2_core/output"

# RKNN-LLM (external repo at workspace root)
RKNN_LLM_BASE="$PROJECT_DIR/../rknn-llm"
# HuggingFace model download base URLs (override via BUDDY_* env vars)
MELO_TTS_HF_BASE="${BUDDY_MELO_TTS_HF_BASE:-https://huggingface.co/voicialex/melo-tts-rknn/resolve/main}"
ZIPFORMER_RKNN_HF_BASE="${BUDDY_ZIPFORMER_RKNN_HF_BASE:-https://huggingface.co/voicialex/zipformer-asr-rknn/resolve/main/rknn}"
ZIPFORMER_ONNX_HF_BASE="${BUDDY_ZIPFORMER_ONNX_HF_BASE:-https://huggingface.co/voicialex/zipformer-asr-rknn/resolve/main/onnx}"
FACE_EMOTION_HF_BASE="${BUDDY_FACE_EMOTION_HF_BASE:-https://huggingface.co/voicialex/face-emotion-rknn/resolve/main}"

ROS2_TARBALL_PATH=""
if [ -f "$ROS2_CORE_BASE/humble/${ARCH_NORMALIZED}/ros2-humble-${ARCH_NORMALIZED}.tar.gz" ]; then
    ROS2_TARBALL_PATH="$ROS2_CORE_BASE/humble/${ARCH_NORMALIZED}/ros2-humble-${ARCH_NORMALIZED}.tar.gz"
elif [ -f "$ROS2_CORE_BASE/jazzy/${ARCH_NORMALIZED}/ros2-jazzy-${ARCH_NORMALIZED}.tar.gz" ]; then
    ROS2_TARBALL_PATH="$ROS2_CORE_BASE/jazzy/${ARCH_NORMALIZED}/ros2-jazzy-${ARCH_NORMALIZED}.tar.gz"
fi

# ONNX Runtime
ONNXRT_VERSION="1.24.4"
ONNXRT_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRT_VERSION}/onnxruntime-linux-${ARCH_ORT}-${ONNXRT_VERSION}.tgz"

# Sherpa-ONNX
SHERPA_VERSION="1.13.1"
if [[ "$ARCH_SHERPA" == "x64" ]]; then
    SHERPA_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_VERSION}/sherpa-onnx-v${SHERPA_VERSION}-linux-${ARCH_SHERPA}-shared.tar.bz2"
    SHERPA_EXTRACT_DIR="sherpa-onnx-v${SHERPA_VERSION}-linux-${ARCH_SHERPA}-shared"
else
    SHERPA_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_VERSION}/sherpa-onnx-v${SHERPA_VERSION}-linux-${ARCH_SHERPA}-shared-cpu.tar.bz2"
    SHERPA_EXTRACT_DIR="sherpa-onnx-v${SHERPA_VERSION}-linux-${ARCH_SHERPA}-shared-cpu"
fi

# ONNX Runtime GPU (x86_64 only)
ONNXRT_GPU_VERSION="1.24.4"
if [[ "$ARCH_ORT" == "x64" ]]; then
    ONNXRT_GPU_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRT_GPU_VERSION}/onnxruntime-linux-x64-gpu-${ONNXRT_GPU_VERSION}.tgz"
else
    ONNXRT_GPU_URL=""
fi

# SentencePiece
SENTENCEPIECE_VERSION="0.2.0"
SENTENCEPIECE_URL="https://github.com/google/sentencepiece/archive/refs/tags/v${SENTENCEPIECE_VERSION}.tar.gz"

# MOSS-TTS models
MOSS_TTS_MODEL_DIR="moss-tts-nano"
MOSS_TTS_HF_BASE="https://huggingface.co/OpenMOSS-Team/MOSS-TTS-Nano-100M-ONNX/resolve/main"
MOSS_CODEC_HF_BASE="https://huggingface.co/OpenMOSS-Team/MOSS-Audio-Tokenizer-Nano-ONNX/resolve/main"

source "$SCRIPT_DIR/common.sh"

enable_proxy_for_run() {
    if [[ -z "$PROXY_URL" ]]; then
        return 0
    fi
    export http_proxy="$PROXY_URL"
    export https_proxy="$PROXY_URL"
    export HTTP_PROXY="$PROXY_URL"
    export HTTPS_PROXY="$PROXY_URL"
    export no_proxy="${no_proxy:-localhost,127.0.0.1,::1}"
    export NO_PROXY="$no_proxy"
    log_step "Proxy enabled for this run: $PROXY_URL"
}
enable_proxy_for_run

# ══════════════════════════════════════════════════════════
# 工具函数
# ══════════════════════════════════════════════════════════

download() {
    local url="$1" dest="$2"
    # Keep partial file on network failure so next run can resume with --continue.
    wget --timeout=0 --tries=3 --continue --show-progress --progress=bar:force -O "$dest" "$url" || return 1
    if [ ! -s "$dest" ]; then
        rm -f "$dest"
        return 1
    fi
}

archive_is_complete() {
    local file="$1"
    local format="$2"
    case "$format" in
        tgz) tar tzf "$file" >/dev/null 2>&1 ;;
        tbz2) tar tjf "$file" >/dev/null 2>&1 ;;
        *) return 1 ;;
    esac
}

ensure_archive() {
    local url="$1"
    local dest="$2"
    local format="$3"
    local name="$4"

    if [ -s "$dest" ] && archive_is_complete "$dest" "$format"; then
        log_skip "$name archive cached: $(basename "$dest")"
        return 0
    fi

    if [ -s "$dest" ]; then
        log_step "Resuming $name archive: $(basename "$dest") ..."
    else
        log_step "Downloading $name archive: $(basename "$dest") ..."
    fi

    download "$url" "$dest" || { log_err "Failed to download $name"; return 1; }
    if ! archive_is_complete "$dest" "$format"; then
        log_err "$name archive is incomplete/corrupted (kept for resume): $dest"
        return 1
    fi
}

ort_version_installed() {
    local ort_dir="$1"
    local version="$2"
    [ -f "$ort_dir/lib/libonnxruntime.so.$version" ]
}

# ══════════════════════════════════════════════════════════
# Third-party (git submodules + prebuilt check)
# ══════════════════════════════════════════════════════════

setup_submodules() {
    # No submodules in buddy anymore (all moved to thirdparty repo)
    return 0
}

check_thirdparty() {
    local missing=()
    if [ ! -f "$PREBUILT_DIR/funasr/bin/funasr-wss-server" ]; then
        missing+=("funasr")
    fi
    if [ ! -f "$PREBUILT_DIR/opencv/lib/libopencv_core.so" ]; then
        missing+=("opencv")
    fi
    if [ ${#missing[@]} -eq 0 ]; then
        log_skip "Third-party (funasr + opencv) for ${ARCH_NORMALIZED}"
        return 0
    fi

    # Try to extract from thirdparty tarball (same pattern as ros2_core)
    local THIRDPARTY_BASE="$PROJECT_DIR/../thirdparty/output"
    local THIRDPARTY_TARBALL="$THIRDPARTY_BASE/${ARCH_NORMALIZED}/thirdparty-${ARCH_NORMALIZED}.tar.gz"
    if [ -f "$THIRDPARTY_TARBALL" ]; then
        log_step "Extracting thirdparty from $(basename "$THIRDPARTY_TARBALL") ..."
        tar xzf "$THIRDPARTY_TARBALL" -C "$PREBUILT_DIR"
        log_ok "Third-party (funasr + opencv)"
        return 0
    fi

    log_step "Missing third-party prebuilt: ${missing[*]}"
    echo "       Run: cd ../thirdparty && ./build.sh -t ${ARCH_NORMALIZED}"
    return 0
}

# ══════════════════════════════════════════════════════════
# Prebuilt 下载
# ══════════════════════════════════════════════════════════

setup_ros2_core() {
    if [ -f "$PREBUILT_DIR/ros2_core/setup.bash" ]; then
        log_skip "ROS 2 Core"
        return 0
    fi
    if [ -z "$ROS2_TARBALL_PATH" ]; then
        log_err "No ROS 2 tarball found for ${ARCH_NORMALIZED}"
        echo "       Searched:"
        echo "         - $ROS2_CORE_BASE/humble/${ARCH_NORMALIZED}/ros2-humble-${ARCH_NORMALIZED}.tar.gz"
        echo "         - $ROS2_CORE_BASE/jazzy/${ARCH_NORMALIZED}/ros2-jazzy-${ARCH_NORMALIZED}.tar.gz"
        echo "       Run: cd ~/buddy_ws/ros2_core && ./scripts/docker_build.sh humble --arch ${ARCH_NORMALIZED}"
        return 1
    fi
    log_step "Extracting ROS 2 Core from $(basename "$ROS2_TARBALL_PATH") ..."
    mkdir -p "$PREBUILT_DIR/ros2_core"
    tar xzf "$ROS2_TARBALL_PATH" -C "$PREBUILT_DIR/ros2_core"
    log_ok "ROS 2 Core"
}

setup_onnxruntime() {
    if ort_version_installed "$PREBUILT_DIR/onnxruntime" "$ONNXRT_VERSION"; then
        log_skip "ONNX Runtime v${ONNXRT_VERSION} (${ARCH_ORT})"
        return 0
    fi
    if [ -d "$PREBUILT_DIR/onnxruntime" ]; then
        log_step "Replacing incompatible ONNX Runtime with v${ONNXRT_VERSION} (${ARCH_ORT}) ..."
        rm -rf "$PREBUILT_DIR/onnxruntime"
    fi
    local tmp="$PREBUILT_DIR/onnxruntime-linux-${ARCH_ORT}-${ONNXRT_VERSION}.tgz"
    ensure_archive "$ONNXRT_URL" "$tmp" "tgz" "ONNX Runtime v${ONNXRT_VERSION} (${ARCH_ORT})" || return 1
    tar xzf "$tmp" -C "$PREBUILT_DIR" || { log_err "Failed to extract ONNX Runtime (archive kept): $tmp"; return 1; }
    mv "$PREBUILT_DIR/onnxruntime-linux-${ARCH_ORT}-${ONNXRT_VERSION}" "$PREBUILT_DIR/onnxruntime"
    rm -f "$tmp"
    log_ok "ONNX Runtime v${ONNXRT_VERSION}"
}

setup_sherpa_onnx() {
    if [ -f "$PREBUILT_DIR/sherpa-onnx/lib/libsherpa-onnx-c-api.so" ]; then
        log_skip "Sherpa-ONNX v${SHERPA_VERSION} (${ARCH_SHERPA})"
    else
        local tmp="$PREBUILT_DIR/sherpa-onnx-v${SHERPA_VERSION}.tar.bz2"
        ensure_archive "$SHERPA_URL" "$tmp" "tbz2" "Sherpa-ONNX v${SHERPA_VERSION} (${ARCH_SHERPA})" || return 1
        tar xjf "$tmp" -C "$PREBUILT_DIR" || { log_err "Failed to extract Sherpa-ONNX (archive kept): $tmp"; return 1; }
        mv "$PREBUILT_DIR/${SHERPA_EXTRACT_DIR}" "$PREBUILT_DIR/sherpa-onnx"
        rm -f "$tmp"
        log_ok "Sherpa-ONNX v${SHERPA_VERSION}"
    fi

    # arm64 "-shared-cpu" package lacks headers; fetch from x86 package (headers are platform-independent)
    if [[ ! -d "$PREBUILT_DIR/sherpa-onnx/include" ]]; then
        local x86_include="$PROJECT_DIR/prebuilt/x86_64/sherpa-onnx/include"
        if [[ ! -d "$x86_include" ]]; then
            log_step "Downloading x86 sherpa-onnx for headers ..."
            local x86_url="https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_VERSION}/sherpa-onnx-v${SHERPA_VERSION}-linux-x64-shared.tar.bz2"
            local x86_tmp="/tmp/sherpa-onnx-x64-headers.tar.bz2"
            ensure_archive "$x86_url" "$x86_tmp" "tbz2" "x86 sherpa-onnx headers" || return 1
            local x86_dir="/tmp/sherpa-onnx-v${SHERPA_VERSION}-linux-x64-shared"
            tar xjf "$x86_tmp" -C /tmp || { log_err "Failed to extract x86 sherpa-onnx headers (archive kept): $x86_tmp"; return 1; }
            mkdir -p "$PROJECT_DIR/prebuilt/x86_64/sherpa-onnx"
            cp -r "$x86_dir/include" "$x86_include"
            rm -rf "$x86_tmp" "$x86_dir"
        fi
        cp -r "$x86_include" "$PREBUILT_DIR/sherpa-onnx/include"
        log_step "Copied headers from x86 package (platform-independent)"
    fi
}

setup_onnxruntime_gpu() {
    local tmp="$PREBUILT_DIR/onnxruntime-gpu-${ONNXRT_GPU_VERSION}.tgz"
    local resume_partial=false
    if [ -s "$tmp" ] && ! ort_version_installed "$PREBUILT_DIR/onnxruntime-gpu" "$ONNXRT_GPU_VERSION"; then
        resume_partial=true
    fi

    if [ "$WITH_GPU_ORT" != "true" ]; then
        if [ "$resume_partial" = true ]; then
            :
        else
            return 0
        fi
    fi

    if ort_version_installed "$PREBUILT_DIR/onnxruntime-gpu" "$ONNXRT_GPU_VERSION"; then
        log_skip "ONNX Runtime GPU v${ONNXRT_GPU_VERSION}"
        return 0
    fi
    if [ -d "$PREBUILT_DIR/onnxruntime-gpu" ]; then
        log_step "Replacing incompatible ONNX Runtime GPU with v${ONNXRT_GPU_VERSION} ..."
        rm -rf "$PREBUILT_DIR/onnxruntime-gpu"
    fi

    if [ -z "$ONNXRT_GPU_URL" ]; then
        return 0
    fi
    ensure_archive "$ONNXRT_GPU_URL" "$tmp" "tgz" "ONNX Runtime GPU v${ONNXRT_GPU_VERSION}" || return 1
    tar xzf "$tmp" -C "$PREBUILT_DIR" || { log_err "Failed to extract ONNX Runtime GPU (archive kept): $tmp"; return 1; }
    mv "$PREBUILT_DIR/onnxruntime-linux-x64-gpu-${ONNXRT_GPU_VERSION}" "$PREBUILT_DIR/onnxruntime-gpu"
    rm -f "$tmp"
    log_ok "ONNX Runtime GPU v${ONNXRT_GPU_VERSION}"
}

setup_rknn() {
    # RKNN is provided by thirdparty tarball (extracted via check_thirdparty)
    if [ "$ARCH_NORMALIZED" != "aarch64" ]; then
        return 0
    fi
    if [ -f "$PREBUILT_DIR/rknn/lib/librknnrt.so" ]; then
        log_skip "RKNN SDK (rk3588)"
        return 0
    fi
    # RKNN is now provided by thirdparty tarball (extracted via check_thirdparty)
    log_err "RKNN SDK not found. Ensure thirdparty tarball includes rknn."
    echo "       Run: cd ../thirdparty && ./build.sh -t aarch64"
    return 1
}

setup_rkllm() {
    # Only needed for aarch64 builds (RK3588 NPU LLM)
    if [ "$ARCH_NORMALIZED" != "aarch64" ]; then
        return 0
    fi
    if [ -f "$PREBUILT_DIR/rkllm/lib/librkllmrt.so" ]; then
        log_skip "RKLLM Runtime (rk3588)"
        return 0
    fi
    # RKLLM runtime is in external rknn-llm repo at workspace root
    local rkllm_src="$RKNN_LLM_BASE/rkllm-runtime/Linux/librkllm_api/aarch64/librkllmrt.so"
    if [ ! -f "$rkllm_src" ]; then
        log_err "RKLLM Runtime not found: $rkllm_src"
        echo "       Clone rknn-llm to $RKNN_LLM_BASE"
        return 1
    fi
    log_step "Installing RKLLM Runtime (rk3588) ..."
    mkdir -p "$PREBUILT_DIR/rkllm/lib"
    cp "$rkllm_src" "$PREBUILT_DIR/rkllm/lib/"
    # Also copy to flask_server's expected location
    local flask_lib_dir="$RKNN_LLM_BASE/examples/rkllm_server_demo/rkllm_server/lib"
    mkdir -p "$flask_lib_dir"
    cp "$rkllm_src" "$flask_lib_dir/"
    log_ok "RKLLM Runtime"
}

setup_sentencepiece() {
    if [ -f "$PREBUILT_DIR/sentencepiece/lib/libsentencepiece.so" ]; then
        log_skip "SentencePiece v${SENTENCEPIECE_VERSION}"
        return 0
    fi
    local tmp_dir="$PREBUILT_DIR/_sentencepiece_build"
    local tmp="$tmp_dir/sentencepiece.tar.gz"
    mkdir -p "$tmp_dir"
    ensure_archive "$SENTENCEPIECE_URL" "$tmp" "tgz" "SentencePiece v${SENTENCEPIECE_VERSION} source" || return 1
    log_step "Building SentencePiece v${SENTENCEPIECE_VERSION} from source ..."
    tar xzf "$tmp" -C "$tmp_dir"
    local src_dir="$tmp_dir/sentencepiece-${SENTENCEPIECE_VERSION}"
    mkdir -p "$src_dir/build"
    cd "$src_dir/build"
    cmake .. -DCMAKE_INSTALL_PREFIX="$PREBUILT_DIR/sentencepiece" \
             -DSPM_ENABLE_SHARED=ON -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_POSITION_INDEPENDENT_CODE=ON >/dev/null 2>&1
    make -j"$(nproc)" >/dev/null 2>&1
    make install >/dev/null 2>&1
    cd "$PROJECT_DIR"
    rm -rf "$tmp_dir"
    log_ok "SentencePiece v${SENTENCEPIECE_VERSION}"
}

# ══════════════════════════════════════════════════════════
# 模型下载
# ══════════════════════════════════════════════════════════

setup_model_asr() {
    local name="sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20"
    local target_dir="$MODELS_DIR/$name"
    local required_files=(
        "encoder-epoch-99-avg-1.onnx"
        "decoder-epoch-99-avg-1.onnx"
        "joiner-epoch-99-avg-1.onnx"
        "tokens.txt"
    )
    local f=""
    local missing=()
    for f in "${required_files[@]}"; do
        if [ ! -f "$target_dir/$f" ]; then
            missing+=("$f")
        fi
    done
    if [ ${#missing[@]} -eq 0 ]; then
        log_skip "ASR model ($name)"
        return 0
    fi

    mkdir -p "$target_dir"
    log_step "Downloading ASR ONNX model ($name) from HF ..."
    for f in "${required_files[@]}"; do
        download "$ZIPFORMER_ONNX_HF_BASE/$f" "$target_dir/$f" || {
            log_err "Failed to download ASR ONNX file: $f"
            return 1
        }
    done
    log_ok "ASR model ($name)"
}

setup_model_asr_rknn() {
    if [ "$ARCH_NORMALIZED" != "aarch64" ]; then
        return 0
    fi
    local name="zipformer-rknn"
    local target_dir="$MODELS_DIR/$name"
    local required_files=(encoder.rknn decoder.rknn joiner.rknn tokens.txt)
    local missing=()
    local f=""
    for f in "${required_files[@]}"; do
        if [ ! -f "$target_dir/$f" ]; then
            missing+=("$f")
        fi
    done
    if [ ${#missing[@]} -eq 0 ]; then
        log_skip "ASR RKNN model ($name)"
        return 0
    fi

    mkdir -p "$target_dir"
    log_step "Downloading ASR RKNN model ($name) from HF ..."
    for f in "${required_files[@]}"; do
        download "$ZIPFORMER_RKNN_HF_BASE/$f" "$target_dir/$f" || {
            log_err "Failed to download ASR RKNN file: $f"
            return 1
        }
    done
    log_ok "ASR RKNN model ($name)"
}

setup_model_kws() {
    local name="sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile"
    local url="https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/${name}.tar.bz2"
    if [ -f "$MODELS_DIR/$name/tokens.txt" ]; then
        cp "$PROJECT_DIR/src/buddy_app/params/keywords.txt" "$MODELS_DIR/$name/keywords.txt"
        log_skip "KWS model ($name)"
        return 0
    fi
    log_step "Downloading KWS model ($name) ..."
    local tmp="$MODELS_DIR/${name}.tar.bz2"
    download "$url" "$tmp" || { log_err "Failed to download KWS model"; return 1; }
    tar xjf "$tmp" -C "$MODELS_DIR" || { rm -f "$tmp"; log_err "Failed to extract KWS model"; return 1; }
    rm -f "$tmp"
    cp "$PROJECT_DIR/src/buddy_app/params/keywords.txt" "$MODELS_DIR/$name/keywords.txt"
    log_ok "KWS model ($name)"
}

setup_model_tts() {
    local name="kokoro-int8-multi-lang-v1_1"
    local url="https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/${name}.tar.bz2"
    if [ -f "$MODELS_DIR/$name/model.int8.onnx" ]; then
        log_skip "TTS model ($name)"
        return 0
    fi
    log_step "Downloading TTS model ($name) ..."
    local tmp="$MODELS_DIR/${name}.tar.bz2"
    download "$url" "$tmp" || { log_err "Failed to download TTS model"; return 1; }
    tar xjf "$tmp" -C "$MODELS_DIR" || { rm -f "$tmp"; log_err "Failed to extract TTS model"; return 1; }
    rm -f "$tmp"
    log_ok "TTS model ($name)"
}

setup_model_tts_melo() {
    local name="vits-melo-tts-zh_en"
    local url="https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/${name}.tar.bz2"
    if [ -f "$MODELS_DIR/$name/model.onnx" ]; then
        log_skip "TTS MeloTTS model ($name)"
        return 0
    fi
    log_step "Downloading MeloTTS model ($name) ..."
    local tmp="$MODELS_DIR/${name}.tar.bz2"
    download "$url" "$tmp" || { log_err "Failed to download MeloTTS model"; return 1; }
    tar xjf "$tmp" -C "$MODELS_DIR" || { rm -f "$tmp"; log_err "Failed to extract MeloTTS model"; return 1; }
    rm -f "$tmp"
    log_ok "TTS MeloTTS model ($name)"
}

setup_model_tts_melo_rknn() {
    local name="melo-tts-rknn"
    local target_dir="$MODELS_DIR/$name"
    local required_relpaths=(
        "checkpoint/rknn/configuration.json"
        "checkpoint/rknn/tokenizer.json"
        "checkpoint/rknn/vocab.txt"
        "checkpoint/rknn/bert_lml_model.rknn"
        "checkpoint/rknn/prior_model.rknn"
        "checkpoint/rknn/flow_model.rknn"
        "checkpoint/rknn/decoder_frame31.rknn"
        "model/MeloTTS-ONNX/melo_onnx/text/opencpop-strict.txt"
        "model/MeloTTS-ONNX/melo_onnx/text/tone_sandhi.py"
        "third_party_data/jieba/dict.txt"
        "third_party_data/pypinyin/pinyin_dict.json"
        "third_party_data/pypinyin/phrases_dict.json"
    )

    if [ "$ARCH_NORMALIZED" != "aarch64" ]; then
        return 0
    fi

    local missing=0
    for rel in "${required_relpaths[@]}"; do
        if [ ! -f "$target_dir/$rel" ]; then
            missing=1
            break
        fi
    done
    if [ "$missing" -eq 0 ]; then
        log_skip "TTS Melo RKNN model ($name)"
        return 0
    fi

    log_step "Downloading MeloTTS RKNN model ($name) from HF ..."
    mkdir -p "$target_dir/checkpoint/rknn" \
             "$target_dir/model/MeloTTS-ONNX/melo_onnx/text" \
             "$target_dir/third_party_data/jieba" \
             "$target_dir/third_party_data/pypinyin"
    for rel in "${required_relpaths[@]}"; do
        download "$MELO_TTS_HF_BASE/$rel" "$target_dir/$rel" || {
            log_err "Failed to download MeloTTS file: $rel"
            return 1
        }
    done
    log_ok "TTS Melo RKNN model ($name)"
}

setup_model_tts_vits() {
    local name="vits-icefall-zh-aishell3"
    local url="https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/${name}.tar.bz2"
    if [ -f "$MODELS_DIR/$name/model.onnx" ]; then
        log_skip "TTS VITS model ($name)"
        return 0
    fi
    log_step "Downloading VITS TTS model ($name) ..."
    local tmp="$MODELS_DIR/${name}.tar.bz2"
    download "$url" "$tmp" || { log_err "Failed to download VITS TTS model"; return 1; }
    tar xjf "$tmp" -C "$MODELS_DIR" || { rm -f "$tmp"; log_err "Failed to extract VITS TTS model"; return 1; }
    rm -f "$tmp"
    log_ok "TTS VITS model ($name)"
}

setup_model_funasr() {
    local offline_dir="$MODELS_DIR/funasr-paraformer-zh-offline"
    local online_dir="$MODELS_DIR/funasr-paraformer-zh-online"
    local vad_dir="$MODELS_DIR/funasr-vad"

    if [ -f "$offline_dir/model_quant.onnx" ] && \
       [ -f "$online_dir/model_quant.onnx" ] && \
       [ -f "$vad_dir/model_quant.onnx" ]; then
        log_skip "FunASR (paraformer-zh offline + online + vad)"
        return 0
    fi

    if ! command -v modelscope >/dev/null 2>&1; then
        log_err "modelscope CLI not found. Install: pip install modelscope"
        return 0
    fi

    log_step "Downloading FunASR models ..."
    local missing=0

    if [ ! -f "$offline_dir/model_quant.onnx" ]; then
        mkdir -p "$offline_dir"
        modelscope download \
            --model iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-onnx \
            --local_dir "$offline_dir" || { log_err "FunASR offline download failed"; missing=1; }
    fi

    if [ ! -f "$online_dir/model_quant.onnx" ]; then
        mkdir -p "$online_dir"
        modelscope download \
            --model iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-online-onnx \
            --local_dir "$online_dir" || { log_err "FunASR online download failed"; missing=1; }
    fi

    if [ ! -f "$vad_dir/model_quant.onnx" ]; then
        mkdir -p "$vad_dir"
        modelscope download \
            --model iic/speech_fsmn_vad_zh-cn-16k-common-onnx \
            --local_dir "$vad_dir" || { log_err "FunASR VAD download failed"; missing=1; }
    fi

    [[ "$missing" -eq 0 ]] || return 0
    log_ok "FunASR (paraformer-zh offline + online + vad)"
}

setup_model_moss_tts() {
    local model_dir="$MODELS_DIR/$MOSS_TTS_MODEL_DIR"
    local tts_dir="$model_dir/MOSS-TTS-Nano-100M-ONNX"
    local codec_dir="$model_dir/MOSS-Audio-Tokenizer-Nano-ONNX"

    if [ -f "$tts_dir/moss_tts_global_shared.data" ] && [ -f "$codec_dir/moss_audio_tokenizer_decode_shared.data" ]; then
        log_skip "MOSS-TTS Nano models"
        return 0
    fi

    log_step "Downloading MOSS-TTS Nano models (this may take several minutes) ..."
    mkdir -p "$tts_dir" "$codec_dir"

    local tts_files=(
        "browser_poc_manifest.json"
        "tts_browser_onnx_meta.json"
        "tokenizer.model"
        "moss_tts_prefill.onnx"
        "moss_tts_decode_step.onnx"
        "moss_tts_local_cached_step.onnx"
        "moss_tts_local_decoder.onnx"
        "moss_tts_local_fixed_sampled_frame.onnx"
        "moss_tts_global_shared.data"
        "moss_tts_local_shared.data"
    )
    for f in "${tts_files[@]}"; do
        if [ ! -f "$tts_dir/$f" ]; then
            log_step "  ↓ MOSS-TTS/$f"
            download "$MOSS_TTS_HF_BASE/$f" "$tts_dir/$f" || { log_err "Failed: $f"; return 1; }
        fi
    done

    local codec_files=(
        "codec_browser_onnx_meta.json"
        "moss_audio_tokenizer_decode_full.onnx"
        "moss_audio_tokenizer_decode_step.onnx"
        "moss_audio_tokenizer_encode.onnx"
        "moss_audio_tokenizer_decode_shared.data"
        "moss_audio_tokenizer_encode.data"
    )
    for f in "${codec_files[@]}"; do
        if [ ! -f "$codec_dir/$f" ]; then
            log_step "  ↓ MOSS-Codec/$f"
            download "$MOSS_CODEC_HF_BASE/$f" "$codec_dir/$f" || { log_err "Failed: $f"; return 1; }
        fi
    done
    log_ok "MOSS-TTS Nano models"
}

setup_model_emotion() {
    local name="face_emotion"
    local target_dir="$MODELS_DIR/$name"
    local required_files=(
        "retinaface_mnet_v2_fp16.onnx"
        "retinaface_mnet_v2_fp16.rknn"
        "affecnet7_fp16.onnx"
        "affecnet7_fp16.rknn"
    )

    local missing=()
    local f=""
    for f in "${required_files[@]}"; do
        if [ ! -f "$target_dir/$f" ]; then
            missing+=("$f")
        fi
    done
    if [ ${#missing[@]} -eq 0 ]; then
        log_skip "Vision model ($name)"
        return 0
    fi

    mkdir -p "$target_dir"
    log_step "Downloading vision model ($name) from HF ..."
    for f in "${required_files[@]}"; do
        local subdir="onnx"
        [[ "$f" == *.rknn ]] && subdir="rknn"
        download "$FACE_EMOTION_HF_BASE/$subdir/$f" "$target_dir/$f" || {
            log_err "Failed to download vision file: $f"
            return 1
        }
    done
    log_ok "Vision model ($name)"
}

# ══════════════════════════════════════════════════════════
# 编排
# ══════════════════════════════════════════════════════════

do_prebuilt() {
    mkdir -p "$PREBUILT_DIR"

    setup_submodules
    log_stage "Prebuilt Libraries (${ARCH_NORMALIZED})"
    check_thirdparty
    setup_ros2_core
    setup_onnxruntime
    setup_sherpa_onnx
    setup_onnxruntime_gpu
    setup_sentencepiece
    setup_rknn
    setup_rkllm

    ln -sfn "${ARCH_NORMALIZED}" "$PROJECT_DIR/prebuilt/current"
    log_ok "Symlink: prebuilt/current -> ${ARCH_NORMALIZED}"
}

check_model_chattts() {
    local model_dir="$MODELS_DIR/ChatTTS"
    if [ -f "$model_dir/asset/Vocos.safetensors" ]; then
        log_skip "ChatTTS model"
        return 0
    fi
    echo ""
    log_step "[Manual] ChatTTS (~1.2GB) — auto-downloads on first tts_server.py run"
    echo "       Or manually download:"
    echo "         pip install ChatTTS"
    echo "         python -c \"import ChatTTS; c=ChatTTS.Chat(); c.load(custom_path='$model_dir')\""
    echo "       Requires network access to huggingface.co"
}

check_model_ollama() {
    if command -v ollama &>/dev/null && ollama list 2>/dev/null | grep -q "qwen2.5:7b"; then
        log_skip "Ollama model (qwen2.5:7b)"
        return 0
    fi
    echo ""
    log_step "[Manual] Ollama LLM model (qwen2.5:7b, ~4.4GB)"
    echo "       Install ollama:"
    echo "         curl -fsSL https://ollama.com/install.sh | sh"
    echo "       Pull model:"
    echo "         ollama pull qwen2.5:7b"
}

do_models() {
    log_stage "Models (${ARCH_NORMALIZED})"
    mkdir -p "$MODELS_DIR"
    if [ "$ARCH_NORMALIZED" != "aarch64" ]; then
        setup_model_asr
    fi
    setup_model_asr_rknn
    setup_model_kws
    setup_model_tts
    setup_model_tts_melo
    setup_model_tts_melo_rknn
    setup_model_funasr
    setup_model_moss_tts
    setup_model_emotion

    log_stage "Manual Setup (optional)"
    check_model_chattts
    check_model_ollama
}

# ══════════════════════════════════════════════════════════
# Main
# ══════════════════════════════════════════════════════════

do_prebuilt
do_models

echo ""
echo "=== Done ==="
echo "Next: ./scripts/start_llm_server.sh && ./scripts/start_tts_server.sh && ./build.sh && ./run.sh"
