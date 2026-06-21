#!/usr/bin/env bash
# Prebuilt 依赖 + Buddy 模型 一键安装脚本
#
# 用法:
#   ./scripts/setup_prebuilt.sh          # 下载 x86_64 + aarch64 的全部 prebuilt + 模型
#   ./scripts/setup_prebuilt.sh -t x86   # 只下载 x86_64
#   ./scripts/setup_prebuilt.sh -t arm64 # 只下载 aarch64
#
# 跳过逻辑: 已存在的文件不会重复下载
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# ══════════════════════════════════════════════════════════
# 参数解析 + 架构检测
# ══════════════════════════════════════════════════════════

ARCH="${BUDDY_ARCH:-$(uname -m)}"
ARCH_SPECIFIED=false
PROXY_URL="${BUDDY_PROXY_URL:-}"
print_help() {
    local clash_port=""
    clash_port="$(ss -lntp 2>/dev/null | awk '/clash|verge/ && /127.0.0.1:/ { split($4,a,":"); print a[length(a)]; exit }')"

    cat <<EOF
Usage: ./scripts/setup_prebuilt.sh [OPTIONS]

Download prebuilt libraries and AI models for buddy.
Skips anything already present — safe to re-run.

Options:
  -t, --arch <x86|arm64>   Target arch (default: both x86_64 + aarch64)
  --proxy <url>            HTTP proxy for this run
  -h, --help               Show this help
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

Override via env vars:
  BUDDY_ROS2_TAG=<tag>         ROS 2 Core release tag (default: v2026.06.2)
  BUDDY_ROS2_DISTRO=<distro>   ROS 2 distro (default: humble)
  BUDDY_THIRDPARTY_TAG=<tag>   Third-party release tag (default: v2026.06.16)
  BUDDY_ZIPFORMER_RKNN_HF_BASE  BUDDY_ZIPFORMER_ONNX_HF_BASE
  BUDDY_MELO_TTS_HF_BASE        BUDDY_FACE_EMOTION_HF_BASE

Examples:
  ./scripts/setup_prebuilt.sh                              # Everything (both x86_64 + aarch64)
  ./scripts/setup_prebuilt.sh -t arm64                     # Only aarch64
  ./scripts/setup_prebuilt.sh -t x86                       # Only x86_64
  ./scripts/setup_prebuilt.sh --proxy http://127.0.0.1:7890
  BUDDY_ROS2_TAG=v2026.06.2 ./scripts/setup_prebuilt.sh   # Pin release version
  BUDDY_ROS2_DISTRO=jazzy ./scripts/setup_prebuilt.sh     # Use Jazzy instead of Humble
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            print_help
            exit 0
            ;;
        -t|--arch) ARCH="$2"; ARCH_SPECIFIED=true; shift 2 ;;
        --proxy) PROXY_URL="$2"; shift 2 ;;
        *)
            echo "[ERROR] Unknown option: $1"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
    esac
done

case "$ARCH" in
    x86_64|amd64|x86) ARCH_NORMALIZED="x86_64" ;;
    aarch64|arm64)    ARCH_NORMALIZED="aarch64" ;;
    *) echo "[ERROR] Unsupported arch: $ARCH (use x86, x86_64, amd64, arm64, aarch64)"; exit 1 ;;
esac

# Determine which archs to process
if [[ "$ARCH_SPECIFIED" == true ]]; then
    ARCHS=("$ARCH_NORMALIZED")
else
    ARCHS=("x86_64" "aarch64")
fi

MODELS_DIR="$PROJECT_DIR/models"

cd "$PROJECT_DIR"

# ══════════════════════════════════════════════════════════
# 可配置变量
# ══════════════════════════════════════════════════════════

# ROS 2 tarball
ROS2_DISTRO="${BUDDY_ROS2_DISTRO:-humble}"
ROS2_CORE_BASE="$PROJECT_DIR/../ros2_core/output"

# HuggingFace model download base URLs (override via BUDDY_* env vars)
MELO_TTS_HF_BASE="${BUDDY_MELO_TTS_HF_BASE:-https://huggingface.co/voicialex/melo-tts-rknn/resolve/main}"
ZIPFORMER_RKNN_HF_BASE="${BUDDY_ZIPFORMER_RKNN_HF_BASE:-https://huggingface.co/voicialex/zipformer-asr-rknn/resolve/main/rknn}"
ZIPFORMER_ONNX_HF_BASE="${BUDDY_ZIPFORMER_ONNX_HF_BASE:-https://huggingface.co/voicialex/zipformer-asr-rknn/resolve/main/onnx}"
FACE_EMOTION_HF_BASE="${BUDDY_FACE_EMOTION_HF_BASE:-https://huggingface.co/voicialex/face-emotion-rknn/resolve/main}"

# GitHub Release fallback (override via BUDDY_* env vars)
THIRDPARTY_RELEASE_TAG="${BUDDY_THIRDPARTY_TAG:-v2026.06.16}"
THIRDPARTY_RELEASE_URL="https://github.com/voicialex/thirdparty/releases/download/${THIRDPARTY_RELEASE_TAG}"
ROS2_RELEASE_TAG="${BUDDY_ROS2_TAG:-v2026.06.2}"
ROS2_RELEASE_URL="https://github.com/voicialex/ros2_core/releases/download/${ROS2_RELEASE_TAG}"

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
    wget -q --timeout=0 --tries=3 --continue --show-progress --progress=bar:force \
        -O "$dest" "$url" || return 1
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

# ══════════════════════════════════════════════════════════
# Third-party (git submodules + prebuilt check)
# ══════════════════════════════════════════════════════════
# Third-party prebuilt check
# ══════════════════════════════════════════════════════════

check_thirdparty() {
    local missing=()
    if [ ! -f "$PREBUILT_DIR/funasr/bin/funasr-wss-server" ]; then
        missing+=("funasr")
    fi
    if [ ! -f "$PREBUILT_DIR/opencv/lib/libopencv_core.so" ]; then
        missing+=("opencv")
    fi
    if [ ! -f "$PREBUILT_DIR/onnxruntime/lib/libonnxruntime.so" ]; then
        missing+=("onnxruntime")
    fi
    if [ ! -f "$PREBUILT_DIR/sherpa-onnx/lib/libsherpa-onnx-c-api.so" ]; then
        missing+=("sherpa-onnx")
    fi
    if [ "$ARCH_NORMALIZED" == "x86_64" ] && [ ! -f "$PREBUILT_DIR/onnxruntime-gpu/lib/libonnxruntime.so" ]; then
        missing+=("onnxruntime-gpu")
    fi
    if [ "$ARCH_NORMALIZED" == "aarch64" ] && [ ! -f "$PREBUILT_DIR/rknn/lib/librknnrt.so" ]; then
        missing+=("rknn")
    fi
    if [ "$ARCH_NORMALIZED" == "aarch64" ] && [ ! -f "$PREBUILT_DIR/rkllm/lib/librkllmrt.so" ]; then
        missing+=("rkllm")
    fi
    if [ ${#missing[@]} -eq 0 ]; then
        log_skip "Third-party (funasr + opencv + onnxruntime + onnxruntime-gpu + sherpa-onnx + rknn + rkllm) for ${ARCH_NORMALIZED}"
        return 0
    fi

    # Try to extract from thirdparty tarball (same pattern as ros2_core)
    local THIRDPARTY_BASE="$PROJECT_DIR/../thirdparty/output"
    local THIRDPARTY_TARBALL="$THIRDPARTY_BASE/${ARCH_NORMALIZED}/thirdparty-${ARCH_NORMALIZED}.tar.gz"
    if [ -f "$THIRDPARTY_TARBALL" ]; then
        log_step "Extracting thirdparty from $(basename "$THIRDPARTY_TARBALL") ..."
        tar xzf "$THIRDPARTY_TARBALL" -C "$PREBUILT_DIR"
        log_ok "Third-party (funasr + opencv + onnxruntime + onnxruntime-gpu + sherpa-onnx + rknn + rkllm)"
        return 0
    fi

    # Fallback: download from GitHub Release
    local THIRDPARTY_URL="${THIRDPARTY_RELEASE_URL}/thirdparty-${ARCH_NORMALIZED}.tar.gz"
    local THIRDPARTY_TMP="/tmp/thirdparty-${ARCH_NORMALIZED}-${THIRDPARTY_RELEASE_TAG}.tar.gz"
    log_step "Downloading thirdparty from GitHub Release (${THIRDPARTY_RELEASE_TAG}) ..."
    if download "$THIRDPARTY_URL" "$THIRDPARTY_TMP"; then
        tar xzf "$THIRDPARTY_TMP" -C "$PREBUILT_DIR"
        rm -f "$THIRDPARTY_TMP"
        log_ok "Third-party (funasr + opencv + onnxruntime + onnxruntime-gpu + sherpa-onnx + rknn + rkllm)"
        return 0
    fi
    rm -f "$THIRDPARTY_TMP"

    log_step "Missing third-party prebuilt: ${missing[*]}"
    echo "       Run: cd ../thirdparty && ./build.sh -t ${ARCH_NORMALIZED}"
    echo "       Or set: BUDDY_THIRDPARTY_TAG=<tag> to use a different release"
    return 0
}

# ══════════════════════════════════════════════════════════
# Prebuilt 下载
# ══════════════════════════════════════════════════════════

setup_ros2_core() {
    # Check if the correct distro is already installed
    local distro_marker="$PREBUILT_DIR/ros2_core/.buddy_ros2_distro"
    if [ -f "$PREBUILT_DIR/ros2_core/setup.bash" ] && [ -f "$distro_marker" ] && [ "$(cat "$distro_marker")" = "$ROS2_DISTRO" ]; then
        log_skip "ROS 2 Core (${ROS2_DISTRO})"
        return 0
    fi

    # Wrong distro or no marker → replace
    if [ -d "$PREBUILT_DIR/ros2_core" ]; then
        log_step "Replacing ROS 2 Core (wrong distro or version)"
        rm -rf "$PREBUILT_DIR/ros2_core"
    fi

    local tarball=""
    local cleanup_tarball=false

    if [ -n "$ROS2_TARBALL_PATH" ]; then
        tarball="$ROS2_TARBALL_PATH"
    else
        # Fallback: download from GitHub Release
        local ROS2_URL="${ROS2_RELEASE_URL}/ros2-${ROS2_DISTRO}-${ARCH_NORMALIZED}.tar.gz"
        local ROS2_TMP="/tmp/ros2-${ROS2_DISTRO}-${ARCH_NORMALIZED}-${ROS2_RELEASE_TAG}.tar.gz"
        log_step "Downloading ROS 2 Core from GitHub Release (${ROS2_RELEASE_TAG}, ${ROS2_DISTRO}) ..."
        if download "$ROS2_URL" "$ROS2_TMP"; then
            tarball="$ROS2_TMP"
            cleanup_tarball=true
        else
            rm -f "$ROS2_TMP"
            log_err "No ROS 2 tarball found for ${ARCH_NORMALIZED}"
            echo "       Searched:"
            echo "         - $ROS2_CORE_BASE/${ROS2_DISTRO}/${ARCH_NORMALIZED}/ros2-${ROS2_DISTRO}-${ARCH_NORMALIZED}.tar.gz"
            echo "         - ${ROS2_RELEASE_URL}/ros2-${ROS2_DISTRO}-${ARCH_NORMALIZED}.tar.gz"
            echo "       Run: cd ../ros2_core && ./scripts/build.sh --arch ${ARCH_NORMALIZED}"
            echo "       Or set: BUDDY_ROS2_TAG=<tag> / BUDDY_ROS2_DISTRO=<distro>"
            return 1
        fi
    fi

    log_step "Extracting ROS 2 Core (${ROS2_DISTRO}) from $(basename "$tarball") ..."
    mkdir -p "$PREBUILT_DIR/ros2_core"
    tar xzf "$tarball" -C "$PREBUILT_DIR/ros2_core"
    if [ "$cleanup_tarball" = true ]; then
        rm -f "$tarball"
    fi
    echo "$ROS2_DISTRO" > "$distro_marker"
    log_ok "ROS 2 Core (${ROS2_DISTRO})"
}

ensure_rkllm_server_lib() {
    # flask_server.py loads ./lib/librkllmrt.so relative to its directory
    local lib_dst="$PROJECT_DIR/rkllm_server/lib/librkllmrt.so"
    local lib_src="$PREBUILT_DIR/rkllm/lib/librkllmrt.so"
    if [ -L "$lib_dst" ] || [ -f "$lib_dst" ]; then
        return 0
    fi
    if [ ! -f "$lib_src" ]; then
        return 0  # check_thirdparty already reported the error
    fi
    mkdir -p "$(dirname "$lib_dst")"
    ln -sf "$lib_src" "$lib_dst"
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

    log_stage "Prebuilt Libraries (${ARCH_NORMALIZED})"
    check_thirdparty
    setup_ros2_core
    setup_sentencepiece
    ensure_rkllm_server_lib

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

# ── Per-arch setup ──────────────────────────────────────────

HOST_ARCH="$(uname -m)"
case "$HOST_ARCH" in
    x86_64|amd64) HOST_ARCH_NORMALIZED="x86_64" ;;
    aarch64|arm64) HOST_ARCH_NORMALIZED="aarch64" ;;
    *) HOST_ARCH_NORMALIZED="x86_64" ;;  # fallback
esac

for CURRENT_ARCH in "${ARCHS[@]}"; do
    ARCH_NORMALIZED="$CURRENT_ARCH"
    PREBUILT_DIR="$PROJECT_DIR/prebuilt/${ARCH_NORMALIZED}"

    # Recompute ROS 2 tarball path per arch
    ROS2_TARBALL_PATH=""
    if [ -f "$ROS2_CORE_BASE/$ROS2_DISTRO/${ARCH_NORMALIZED}/ros2-${ROS2_DISTRO}-${ARCH_NORMALIZED}.tar.gz" ]; then
        ROS2_TARBALL_PATH="$ROS2_CORE_BASE/$ROS2_DISTRO/${ARCH_NORMALIZED}/ros2-${ROS2_DISTRO}-${ARCH_NORMALIZED}.tar.gz"
    fi

    do_prebuilt
    do_models
done

# Symlink prebuilt/current → host arch (last in loop may not be host)
ln -sfn "${HOST_ARCH_NORMALIZED}" "$PROJECT_DIR/prebuilt/current"
log_ok "Symlink: prebuilt/current -> ${HOST_ARCH_NORMALIZED}"

echo ""
echo "=== Done ==="
echo "Next: ./scripts/start_llm_server.sh && ./scripts/start_tts_server.sh && ./build.sh && ./run.sh"
