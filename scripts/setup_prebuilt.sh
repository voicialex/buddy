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
Usage: ./scripts/setup_prebuilt.sh [OPTIONS] [COMMAND]

Commands:
  all       Install prebuilt libs + all models (default)
  prebuilt  Only prebuilt libs (ros2_core, onnxruntime, sherpa-onnx, sentencepiece, rkllm)
  models    Only models (ASR, KWS, TTS, FunASR, MOSS-TTS, Vision, ChatTTS hint, Ollama hint)
  vision    Only vision models (face_emotion; legacy emotion fallback)
  funasr    Only FunASR runtime + model
  moss-tts  Only MOSS-TTS dependencies + models

Options:
  --arch <x86_64|aarch64|arm64>   Target architecture (default: host)
  --proxy <url>                   One-shot proxy for this run
  --with-gpu-ort                  Download onnxruntime-gpu (x86_64 only, optional; partial cache auto-resumes)
  -h, --help                      Show this help

Proxy quick start (Clash Verge):
EOF

    if [[ -n "$clash_port" ]]; then
        cat <<EOF
  Detected local Clash Verge proxy: http://127.0.0.1:${clash_port}
  Run:
    ./scripts/setup_prebuilt.sh --proxy http://127.0.0.1:${clash_port} models
EOF
    else
        cat <<'EOF'
  Clash Verge proxy unavailable on this machine (not detected).
  Run without proxy, or pass your own proxy URL:
    ./scripts/setup_prebuilt.sh --proxy http://127.0.0.1:<port> models
EOF
    fi

    cat <<'EOF'

Models overview (stored in models/):
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │ Model                    │ Size   │ Source                │ Auto?               │
  ├─────────────────────────────────────────────────────────────────────────────────┤
  │ ASR RKNN (rk3588)        │ ~130MB │ refs/sdk local mirror │ ✓ auto (arm64 only) │
  │ ASR ONNX (legacy)        │ ~180MB │ Local HF mirror       │ x86 only            │
  │ KWS (keyword spotting)   │ ~13MB  │ GitHub k2-fsa         │ ✓ auto              │
  │ TTS (kokoro-int8)        │ ~207MB │ GitHub k2-fsa         │ ✓ auto              │
  │ TTS Melo RKNN (rk3588)   │ ~505MB │ Local refs/melo-tts   │ ✓ auto (arm64 only) │
  │ FunASR (paraformer-zh)   │ ~800MB │ ModelScope            │ ✓ auto (needs CLI)  │
  │ MOSS-TTS Nano            │ ~500MB │ HuggingFace           │ ✓ auto              │
  │ Vision Face+Emotion      │ ~2.5MB │ Local HF mirror        │ ✓ auto              │
  │ ChatTTS                  │ ~1.2GB │ HuggingFace (auto DL) │ ✗ hint only         │
  │ Ollama (qwen2.5:7b)     │ ~4.4GB │ ollama pull           │ ✗ hint only         │
  │ RKLLM (Qwen3-4B-w8a8)   │ ~3.5GB │ rknn-llm model zoo    │ ✗ manual (NPU only) │
  └─────────────────────────────────────────────────────────────────────────────────┘

Manual download commands:
  # ASR RKNN (RK3588 NPU, recommended)
  # source of truth:
  #   ../refs/temp_code/source_code/sdk/rk3588/model/asr/
  # canonical runtime names:
  cp ../hugging_face/models/asr/zipformer-rknn/{encoder.rknn,decoder.rknn,joiner.rknn,tokens.txt} models/zipformer-rknn/

  # Optional: pull from your own HF repo (set base first)
  # export BUDDY_ZIPFORMER_RKNN_HF_BASE=https://huggingface.co/<you>/<repo>/resolve/main/models/asr/zipformer-rknn
  # wget "$BUDDY_ZIPFORMER_RKNN_HF_BASE/encoder.rknn" -O models/zipformer-rknn/encoder.rknn
  # wget "$BUDDY_ZIPFORMER_RKNN_HF_BASE/decoder.rknn" -O models/zipformer-rknn/decoder.rknn
  # wget "$BUDDY_ZIPFORMER_RKNN_HF_BASE/joiner.rknn" -O models/zipformer-rknn/joiner.rknn
  # wget "$BUDDY_ZIPFORMER_RKNN_HF_BASE/tokens.txt" -O models/zipformer-rknn/tokens.txt

  # ASR ONNX (legacy, x86 only; arm64 NPU does NOT use this)
  cp ../hugging_face/models/asr/zipformer-onnx/{encoder-epoch-99-avg-1.onnx,decoder-epoch-99-avg-1.onnx,joiner-epoch-99-avg-1.onnx,tokens.txt} models/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/
  # Old direct download method (fallback only):
  # wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20.tar.bz2
  # tar xjf sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20.tar.bz2 -C models/

  # KWS
  wget https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile.tar.bz2
  tar xjf sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile.tar.bz2 -C models/

  # TTS (kokoro-int8, default)
  wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-int8-multi-lang-v1_1.tar.bz2
  tar xjf kokoro-int8-multi-lang-v1_1.tar.bz2 -C models/
  # TTS (vits-aishell3, backup)
  wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-icefall-zh-aishell3.tar.bz2
  tar xjf vits-icefall-zh-aishell3.tar.bz2 -C models/
  # TTS (melo, backup)
  wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-melo-tts-zh_en.tar.bz2
  tar xjf vits-melo-tts-zh_en.tar.bz2 -C models/
  # TTS (melo RKNN, arm64 NPU)
  mkdir -p models/melo-tts-rknn/checkpoint/rknn
  cp ../refs/melo-tts/checkpoint/rknn_rk3588/{configuration.json,tokenizer.json,vocab.txt,bert_lml_model.rknn,prior_model.rknn,flow_model.rknn,decoder_frame31.rknn} models/melo-tts-rknn/checkpoint/rknn/
  mkdir -p models/melo-tts-rknn/model/MeloTTS-ONNX/melo_onnx/text
  cp ../refs/melo-tts/model/MeloTTS-ONNX/melo_onnx/text/{opencpop-strict.txt,tone_sandhi.py} models/melo-tts-rknn/model/MeloTTS-ONNX/melo_onnx/text/
  mkdir -p models/melo-tts-rknn/third_party_data/{jieba,pypinyin}
  cp ../refs/melo-tts/third_party_data/jieba/dict.txt models/melo-tts-rknn/third_party_data/jieba/
  cp ../refs/melo-tts/third_party_data/pypinyin/{pinyin_dict.json,phrases_dict.json} models/melo-tts-rknn/third_party_data/pypinyin/

  # FunASR paraformer-zh
  pip install modelscope
  modelscope download --model iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-onnx --local_dir models/funasr-paraformer-zh-offline
  modelscope download --model iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-online-onnx --local_dir models/funasr-paraformer-zh-online
  modelscope download --model iic/speech_fsmn_vad_zh-cn-16k-common-onnx --local_dir models/funasr-vad

  # MOSS-TTS Nano (multiple files from HuggingFace)
  # Run: ./scripts/setup_prebuilt.sh moss-tts

  # Vision face-emotion (retinaface + affecnet)
  mkdir -p models/face_emotion
  cp ../hugging_face/models/vision/face_emotion/{retinaface_mnet_v2_fp16.onnx,retinaface_mnet_v2_fp16.rknn,affecnet7_fp16.onnx,affecnet7_fp16.rknn} models/face_emotion/
  # Optional fallback (legacy Haar model)
  # scp teammate:/path/to/models/emotion/emotion_classifier.onnx models/emotion/

  # ChatTTS (auto-downloads on first tts_server.py run, or manually)
  pip install ChatTTS
  python -c "import ChatTTS; c=ChatTTS.Chat(); c.load(custom_path='models/ChatTTS')"

  # Ollama LLM
  curl -fsSL https://ollama.com/install.sh | sh
  ollama pull qwen2.5:7b

  # RKLLM (NPU builds only — pre-converted models from rknn-llm model zoo)
  # Download from: https://console.box.lenovo.com/l/l0tXb8 (fetch code: rkllm)
  # Place .rkllm files in models/rkllm/
  mkdir -p models/rkllm
  mv Qwen3-4B-rk3588-w8a8.rkllm models/rkllm/

Examples:
  ./scripts/setup_prebuilt.sh                        # Install everything (host arch)
  ./scripts/setup_prebuilt.sh --arch arm64 prebuilt # arm64 prebuilt libs only
  ./scripts/setup_prebuilt.sh --with-gpu-ort prebuilt # include optional onnxruntime-gpu on x86_64
  ./scripts/setup_prebuilt.sh models                 # Download all models
  ./scripts/setup_prebuilt.sh vision                 # Install vision models only
  ./scripts/setup_prebuilt.sh --proxy http://127.0.0.1:<port> models # Download models via proxy
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
        *) break ;;
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
# MeloTTS RKNN reference repo (local workspace reference)
MELO_TTS_REF_ROOT="$PROJECT_DIR/../refs/melo-tts"
# Zipformer RKNN model mirror (maintained outside buddy repo)
ZIPFORMER_RKNN_LOCAL_DIR="${BUDDY_ZIPFORMER_RKNN_LOCAL_DIR:-$PROJECT_DIR/../hugging_face/models/asr/zipformer-rknn}"
# Optional remote HF base:
#   https://huggingface.co/<org>/<repo>/resolve/main/models/asr/zipformer-rknn
ZIPFORMER_RKNN_HF_BASE="${BUDDY_ZIPFORMER_RKNN_HF_BASE:-}"
# Zipformer ONNX mirror (legacy x86 path)
ZIPFORMER_ONNX_LOCAL_DIR="${BUDDY_ZIPFORMER_ONNX_LOCAL_DIR:-$PROJECT_DIR/../hugging_face/models/asr/zipformer-onnx}"
# Optional remote HF base:
#   https://huggingface.co/<org>/<repo>/resolve/main/models/asr/zipformer-onnx
ZIPFORMER_ONNX_HF_BASE="${BUDDY_ZIPFORMER_ONNX_HF_BASE:-}"
# Vision face-emotion mirror (retinaface + affecnet)
FACE_EMOTION_LOCAL_DIR="${BUDDY_FACE_EMOTION_LOCAL_DIR:-$PROJECT_DIR/../hugging_face/models/vision/face_emotion}"
# Optional remote HF base:
#   https://huggingface.co/<org>/<repo>/resolve/main/models/vision/face_emotion
FACE_EMOTION_HF_BASE="${BUDDY_FACE_EMOTION_HF_BASE:-}"
# Vision RKNN reference (rk3588 source-of-truth in local refs)
FACE_EMOTION_RK3588_REF_DIR="${BUDDY_FACE_EMOTION_RK3588_REF_DIR:-$PROJECT_DIR/../refs/temp_code/source_code/sdk/rk3588/model/face_emotion}"

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
            log_skip "ONNX Runtime GPU (optional, enable with --with-gpu-ort)"
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
        log_skip "ONNX Runtime GPU (not available on ${ARCH})"
        return 0
    fi
    ensure_archive "$ONNXRT_GPU_URL" "$tmp" "tgz" "ONNX Runtime GPU v${ONNXRT_GPU_VERSION}" || return 1
    tar xzf "$tmp" -C "$PREBUILT_DIR" || { log_err "Failed to extract ONNX Runtime GPU (archive kept): $tmp"; return 1; }
    mv "$PREBUILT_DIR/onnxruntime-linux-x64-gpu-${ONNXRT_GPU_VERSION}" "$PREBUILT_DIR/onnxruntime-gpu"
    rm -f "$tmp"
    log_ok "ONNX Runtime GPU v${ONNXRT_GPU_VERSION}"
}

setup_rknn() {
    # Only needed for aarch64 builds
    if [ "$ARCH_NORMALIZED" != "aarch64" ]; then
        log_skip "RKNN SDK (x86_64, not needed)"
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
        log_skip "RKLLM Runtime (x86_64, not needed)"
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
    if [ -d "$ZIPFORMER_ONNX_LOCAL_DIR" ]; then
        local local_missing=()
        for f in "${required_files[@]}"; do
            if [ ! -f "$ZIPFORMER_ONNX_LOCAL_DIR/$f" ]; then
                local_missing+=("$f")
            fi
        done
        if [ ${#local_missing[@]} -eq 0 ]; then
            log_step "Installing ASR ONNX model ($name) from local mirror ..."
            cp -f "$ZIPFORMER_ONNX_LOCAL_DIR"/{encoder-epoch-99-avg-1.onnx,decoder-epoch-99-avg-1.onnx,joiner-epoch-99-avg-1.onnx,tokens.txt} "$target_dir/"
            log_ok "ASR model ($name)"
            return 0
        fi
        log_step "Local zipformer ONNX mirror incomplete, fallback to remote HF: missing ${local_missing[*]}"
    fi

    if [ -n "$ZIPFORMER_ONNX_HF_BASE" ]; then
        log_step "Downloading ASR ONNX model ($name) from custom HF ..."
        for f in "${required_files[@]}"; do
            download "$ZIPFORMER_ONNX_HF_BASE/$f" "$target_dir/$f" || {
                log_err "Failed to download ASR ONNX file: $f"
                return 1
            }
        done
        log_ok "ASR model ($name)"
        return 0
    fi

    log_err "ASR ONNX model source not found."
    echo "       Preferred local mirror: $ZIPFORMER_ONNX_LOCAL_DIR"
    echo "       Or set BUDDY_ZIPFORMER_ONNX_HF_BASE to your HF path, e.g.:"
    echo "         export BUDDY_ZIPFORMER_ONNX_HF_BASE=https://huggingface.co/<org>/<repo>/resolve/main/models/asr/zipformer-onnx"
    echo "       Old direct download method (manual fallback):"
    echo "         wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/${name}.tar.bz2"
    echo "         tar xjf ${name}.tar.bz2 -C $MODELS_DIR"
    return 1
}

setup_model_asr_rknn() {
    # Only needed for arm64 (RK3588 NPU)
    if [ "$ARCH_NORMALIZED" != "aarch64" ]; then
        log_skip "ASR RKNN model (x86_64, not needed)"
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
    if [ -d "$ZIPFORMER_RKNN_LOCAL_DIR" ]; then
        local local_missing=()
        for f in "${required_files[@]}"; do
            if [ ! -f "$ZIPFORMER_RKNN_LOCAL_DIR/$f" ]; then
                local_missing+=("$f")
            fi
        done
        if [ ${#local_missing[@]} -eq 0 ]; then
            log_step "Installing ASR RKNN model ($name) from local mirror ..."
            cp -f "$ZIPFORMER_RKNN_LOCAL_DIR"/{encoder.rknn,decoder.rknn,joiner.rknn,tokens.txt} "$target_dir/"
            log_ok "ASR RKNN model ($name)"
            return 0
        fi
        log_step "Local zipformer mirror incomplete, fallback to remote HF: missing ${local_missing[*]}"
    fi

    if [ -z "$ZIPFORMER_RKNN_HF_BASE" ]; then
        log_err "ASR RKNN model source not found."
        echo "       Preferred local mirror: $ZIPFORMER_RKNN_LOCAL_DIR"
        echo "       Or set BUDDY_ZIPFORMER_RKNN_HF_BASE to your HF path, e.g.:"
        echo "         export BUDDY_ZIPFORMER_RKNN_HF_BASE=https://huggingface.co/<org>/<repo>/resolve/main/models/asr/zipformer-rknn"
        return 1
    fi

    log_step "Downloading ASR RKNN model ($name) from custom HF ..."
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
        log_skip "TTS Melo RKNN model ($name, arm64 only)"
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

    if [ ! -d "$MELO_TTS_REF_ROOT" ]; then
        log_err "MeloTTS RKNN source not found: $MELO_TTS_REF_ROOT"
        echo "       Copy these files into models/$name/:"
        printf '         %s\n' "${required_relpaths[@]}"
        return 0
    fi

    local ref_model_dir="$MELO_TTS_REF_ROOT/checkpoint/rknn_rk3588"
    local ref_text_dir="$MELO_TTS_REF_ROOT/model/MeloTTS-ONNX/melo_onnx/text"
    local ref_jieba_dir="$MELO_TTS_REF_ROOT/third_party_data/jieba"
    local ref_pypinyin_dir="$MELO_TTS_REF_ROOT/third_party_data/pypinyin"

    local required_ref_files=(
        "$ref_model_dir/configuration.json"
        "$ref_model_dir/tokenizer.json"
        "$ref_model_dir/vocab.txt"
        "$ref_model_dir/bert_lml_model.rknn"
        "$ref_model_dir/prior_model.rknn"
        "$ref_model_dir/flow_model.rknn"
        "$ref_model_dir/decoder_frame31.rknn"
        "$ref_text_dir/opencpop-strict.txt"
        "$ref_text_dir/tone_sandhi.py"
        "$ref_jieba_dir/dict.txt"
        "$ref_pypinyin_dir/pinyin_dict.json"
        "$ref_pypinyin_dir/phrases_dict.json"
    )
    for ref_file in "${required_ref_files[@]}"; do
        if [ ! -f "$ref_file" ]; then
            log_err "Missing MeloTTS RKNN file: $ref_file"
            return 1
        fi
    done

    log_step "Installing MeloTTS RKNN model set ($name) from local refs ..."
    mkdir -p "$target_dir/checkpoint/rknn"
    mkdir -p "$target_dir/model/MeloTTS-ONNX/melo_onnx/text"
    mkdir -p "$target_dir/third_party_data/jieba"
    mkdir -p "$target_dir/third_party_data/pypinyin"

    cp -f "$ref_model_dir"/{configuration.json,tokenizer.json,vocab.txt,bert_lml_model.rknn,prior_model.rknn,flow_model.rknn,decoder_frame31.rknn} "$target_dir/checkpoint/rknn/"
    cp -f "$ref_text_dir"/{opencpop-strict.txt,tone_sandhi.py} "$target_dir/model/MeloTTS-ONNX/melo_onnx/text/"
    cp -f "$ref_jieba_dir/dict.txt" "$target_dir/third_party_data/jieba/"
    cp -f "$ref_pypinyin_dir"/{pinyin_dict.json,phrases_dict.json} "$target_dir/third_party_data/pypinyin/"
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
    local missing=0

    # Offline model
    if [ -f "$offline_dir/model_quant.onnx" ]; then
        log_skip "FunASR offline model"
    else
        log_step "Downloading FunASR offline model ..."
        mkdir -p "$offline_dir"
        if command -v modelscope >/dev/null 2>&1; then
            modelscope download \
                --model iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-onnx \
                --local_dir "$offline_dir"
        else
            log_err "modelscope CLI not found. Install: pip install modelscope"
            echo "         modelscope download --model iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-onnx --local_dir $offline_dir"
            missing=1
        fi
    fi

    # Online streaming model
    if [ -f "$online_dir/model_quant.onnx" ]; then
        log_skip "FunASR online model"
    else
        log_step "Downloading FunASR online model ..."
        mkdir -p "$online_dir"
        if command -v modelscope >/dev/null 2>&1; then
            modelscope download \
                --model iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-online-onnx \
                --local_dir "$online_dir"
        else
            log_err "modelscope CLI not found. Install: pip install modelscope"
            echo "         modelscope download --model iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-online-onnx --local_dir $online_dir"
            missing=1
        fi
    fi

    # VAD model
    if [ -f "$vad_dir/model_quant.onnx" ]; then
        log_skip "FunASR VAD model"
    else
        log_step "Downloading FunASR VAD model ..."
        mkdir -p "$vad_dir"
        if command -v modelscope >/dev/null 2>&1; then
            modelscope download \
                --model iic/speech_fsmn_vad_zh-cn-16k-common-onnx \
                --local_dir "$vad_dir"
        else
            log_err "modelscope CLI not found. Install: pip install modelscope"
            echo "         modelscope download --model iic/speech_fsmn_vad_zh-cn-16k-common-onnx --local_dir $vad_dir"
            missing=1
        fi
    fi

    [[ "$missing" -eq 0 ]] || return 0  # non-fatal
    log_ok "FunASR models (offline + online + vad)"
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
    local need_refresh=0
    # Guardrail: arm64 must stay aligned with RK3588 source model blobs.
    if [ "$ARCH_NORMALIZED" = "aarch64" ] && [ ${#missing[@]} -eq 0 ] && [ -d "$FACE_EMOTION_RK3588_REF_DIR" ]; then
        local ref_retina="$FACE_EMOTION_RK3588_REF_DIR/retinaface_mnet_v2_fp16.rknn"
        local ref_affect="$FACE_EMOTION_RK3588_REF_DIR/affecnet7_fp16.rknn"
        if [ -f "$ref_retina" ] && [ -f "$ref_affect" ]; then
            if ! cmp -s "$target_dir/retinaface_mnet_v2_fp16.rknn" "$ref_retina" || \
               ! cmp -s "$target_dir/affecnet7_fp16.rknn" "$ref_affect"; then
                need_refresh=1
                log_step "Detected stale vision RKNN blobs, refreshing to RK3588 set ..."
            fi
        fi
    fi
    if [ ${#missing[@]} -eq 0 ] && [ "$need_refresh" -eq 0 ]; then
        log_skip "Vision model ($name)"
        return 0
    fi

    mkdir -p "$target_dir"
    # On arm64, always prefer local RK3588 refs for .rknn if available.
    if [ "$ARCH_NORMALIZED" = "aarch64" ] && [ -d "$FACE_EMOTION_RK3588_REF_DIR" ] && \
       [ -f "$FACE_EMOTION_RK3588_REF_DIR/retinaface_mnet_v2_fp16.rknn" ] && \
       [ -f "$FACE_EMOTION_RK3588_REF_DIR/affecnet7_fp16.rknn" ]; then
        log_step "Installing RK3588 vision RKNN files from local refs ..."
        cp -f "$FACE_EMOTION_RK3588_REF_DIR"/{retinaface_mnet_v2_fp16.rknn,affecnet7_fp16.rknn} "$target_dir/"
        if [ -d "$FACE_EMOTION_LOCAL_DIR" ] && \
           [ -f "$FACE_EMOTION_LOCAL_DIR/retinaface_mnet_v2_fp16.onnx" ] && \
           [ -f "$FACE_EMOTION_LOCAL_DIR/affecnet7_fp16.onnx" ]; then
            cp -f "$FACE_EMOTION_LOCAL_DIR"/{retinaface_mnet_v2_fp16.onnx,affecnet7_fp16.onnx} "$target_dir/"
        fi
        log_ok "Vision model ($name)"
        return 0
    fi

    if [ -d "$FACE_EMOTION_LOCAL_DIR" ]; then
        local local_missing=()
        for f in "${required_files[@]}"; do
            if [ ! -f "$FACE_EMOTION_LOCAL_DIR/$f" ]; then
                local_missing+=("$f")
            fi
        done
        if [ ${#local_missing[@]} -eq 0 ]; then
            log_step "Installing vision model ($name) from local mirror ..."
            cp -f "$FACE_EMOTION_LOCAL_DIR"/{retinaface_mnet_v2_fp16.onnx,retinaface_mnet_v2_fp16.rknn,affecnet7_fp16.onnx,affecnet7_fp16.rknn} "$target_dir/"
            log_ok "Vision model ($name)"
            return 0
        fi
        log_step "Local face_emotion mirror incomplete, fallback to remote HF: missing ${local_missing[*]}"
    fi

    # Fallback: copy RK3588 RKNN files from local refs, ONNX from local mirror if present.
    # This keeps vision usable on rk3588 even when local mirror is stale/incomplete.
    if [ -d "$FACE_EMOTION_RK3588_REF_DIR" ]; then
        local has_rknn_ref=0
        if [ -f "$FACE_EMOTION_RK3588_REF_DIR/retinaface_mnet_v2_fp16.rknn" ] && \
           [ -f "$FACE_EMOTION_RK3588_REF_DIR/affecnet7_fp16.rknn" ]; then
            has_rknn_ref=1
        fi
        if [ "$has_rknn_ref" -eq 1 ]; then
            if [ -f "$FACE_EMOTION_LOCAL_DIR/retinaface_mnet_v2_fp16.onnx" ] && \
               [ -f "$FACE_EMOTION_LOCAL_DIR/affecnet7_fp16.onnx" ]; then
                log_step "Installing vision model ($name) from rk3588 refs + local ONNX ..."
                cp -f "$FACE_EMOTION_LOCAL_DIR"/{retinaface_mnet_v2_fp16.onnx,affecnet7_fp16.onnx} "$target_dir/"
                cp -f "$FACE_EMOTION_RK3588_REF_DIR"/{retinaface_mnet_v2_fp16.rknn,affecnet7_fp16.rknn} "$target_dir/"
                log_ok "Vision model ($name)"
                return 0
            fi
            log_step "RK3588 refs found but ONNX pair missing in local mirror; will continue fallback chain"
        fi
    fi

    if [ -n "$FACE_EMOTION_HF_BASE" ]; then
        log_step "Downloading vision model ($name) from custom HF ..."
        for f in "${required_files[@]}"; do
            download "$FACE_EMOTION_HF_BASE/$f" "$target_dir/$f" || {
                log_err "Failed to download vision file: $f"
                return 1
            }
        done
        log_ok "Vision model ($name)"
        return 0
    fi

    # Legacy fallback: if tiny Haar classifier exists, keep system runnable.
    if [ -f "$MODELS_DIR/emotion/emotion_classifier.onnx" ]; then
        log_skip "Vision model (fallback legacy emotion_classifier.onnx)"
        return 0
    fi

    log_err "Vision face_emotion model source not found."
    echo "       Preferred local mirror: $FACE_EMOTION_LOCAL_DIR"
    echo "       RK3588 refs fallback:   $FACE_EMOTION_RK3588_REF_DIR"
    echo "       Or set BUDDY_FACE_EMOTION_HF_BASE to your HF path, e.g.:"
    echo "         export BUDDY_FACE_EMOTION_HF_BASE=https://huggingface.co/<org>/<repo>/resolve/main/models/vision/face_emotion"
    echo "       Backup fallback (legacy Haar):"
    echo "         scp teammate:/path/to/models/emotion/emotion_classifier.onnx $MODELS_DIR/emotion/"
    return 0  # non-fatal, vision module can be disabled
}

# ══════════════════════════════════════════════════════════
# 编排
# ══════════════════════════════════════════════════════════

do_prebuilt() {
    mkdir -p "$PREBUILT_DIR"

    setup_submodules
    log_stage "Prebuilt [1/2]: Local prebuilts (${ARCH})"
    check_thirdparty
    setup_ros2_core

    log_stage "Prebuilt [2/2]: Download/build missing (${ARCH})"
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
    log_stage "Models: Auto-download"
    mkdir -p "$MODELS_DIR"
    if [ "$ARCH_NORMALIZED" = "aarch64" ]; then
        log_step "Skipping ASR ONNX zipformer model on arm64 (use zipformer-rknn only)"
    else
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

    log_stage "Models: Manual setup (large models)"
    check_model_chattts
    check_model_ollama
}

do_models_vision() {
    log_stage "Models: Vision only"
    mkdir -p "$MODELS_DIR"
    setup_model_emotion
}

# ══════════════════════════════════════════════════════════
# Main
# ══════════════════════════════════════════════════════════

case "${1:-all}" in
    prebuilt) do_prebuilt ;;
    models)   do_models ;;
    vision)   do_models_vision ;;
    funasr)   setup_submodules; check_thirdparty; setup_model_funasr ;;
    moss-tts) setup_onnxruntime_gpu; setup_sentencepiece; setup_model_moss_tts ;;
    all)      do_prebuilt; do_models ;;
    *)
        echo "[ERROR] Unknown command: $1"
        echo "Run '$0 --help' for usage and manual download commands."
        exit 1
        ;;
esac

echo ""
echo "=== Done ==="
echo "Next: ./scripts/start_llm_server.sh && ./scripts/start_tts_server.sh && ./build.sh && ./run.sh"
