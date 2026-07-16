#!/usr/bin/env bash
# Central manifest of model directories. Sourced by:
#   - scripts/setup_prebuilt.sh  (download-side reporting)
#   - docker/Dockerfile          (packaging-side exclude list)
#
# Format per entry (pipe-separated):
#   <dir> | <class> | <devices> | <archs>
#
#   class:   core     — always downloaded + packaged
#            optional — downloaded only with --all-models / BUDDY_ENABLE_*; packaged if present
#            manual   — downloaded at runtime (ChatTTS first-run, ollama pull); never packaged
#            legacy   — superseded by another dir; never packaged
#   devices: space-separated subset of {cpu,gpu,npu} or "all"
#   archs:   space-separated subset of {x86_64,aarch64} or "all"

MODEL_MANIFEST=(
  # ── Core ────────────────────────────────────────────────────────────
  "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20|core|cpu gpu|all"
  "zipformer-rknn|core|npu|aarch64"
  "sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01-mobile|core|all|all"
  "vits-melo-tts-zh_en|core|all|all"
  "melo-tts-rknn|core|npu|aarch64"
  "face_emotion|core|all|all"
  "rkllm|core|npu|aarch64"

  # ── Optional (server-side TTS / extra ASR) ──────────────────────────
  "kokoro-int8-multi-lang-v1_1|optional|cpu gpu|all"
  "funasr-paraformer-zh|optional|cpu gpu|all"
  "funasr-paraformer-zh-offline|optional|cpu gpu|all"
  "funasr-paraformer-zh-online|optional|cpu gpu|all"
  "funasr-vad|optional|cpu gpu|all"
  "moss-tts-nano|optional|cpu gpu|all"

  # ── Manual (runtime download, never packaged) ───────────────────────
  "ChatTTS|manual|cpu gpu|all"
  "ollama|manual|all|all"

  # ── Legacy (superseded, kept locally for reference) ─────────────────
  "emotion|legacy|all|all"
  "vits-icefall-zh-aishell3|legacy|all|all"
  "game|legacy|all|all"
)

# Trim surrounding whitespace from a single string.
_trim() { local s="$1"; s="${s#"${s%%[![:space:]]*}"}"; s="${s%"${s##*[![:space:]]}"}"; printf '%s' "$s"; }

# Parse one manifest entry into the four fields.
# Usage: _parse_entry "$entry" && echo "$M_DIR $M_CLASS $M_DEVICES $M_ARCHS"
_parse_entry() {
    local entry="$1"
    IFS='|' read -r M_DIR M_CLASS M_DEVICES M_ARCHS <<< "$entry"
    M_DIR="$(_trim "$M_DIR")"
    M_CLASS="$(_trim "$M_CLASS")"
    M_DEVICES="$(_trim "$M_DEVICES")"
    M_ARCHS="$(_trim "$M_ARCHS")"
}

# Check whether a space-separated list contains a token (or is "all").
_list_contains() {
    local needle="$1" haystack="$2"
    [[ "$haystack" == "all" ]] && return 0
    local item
    for item in $haystack; do
        [[ "$item" == "$needle" ]] && return 0
    done
    return 1
}

# Echo dirs that should NOT be packaged for the given (device, arch).
# Includes: manual, legacy, and any dir whose devices/archs don't match.
models_exclude_for_device() {
    local device="$1" arch="$2"
    local entry
    for entry in "${MODEL_MANIFEST[@]}"; do
        _parse_entry "$entry" || continue
        case "$M_CLASS" in
            manual|legacy) echo "$M_DIR"; continue ;;
        esac
        if ! _list_contains "$device" "$M_DEVICES"; then
            echo "$M_DIR"; continue
        fi
        if ! _list_contains "$arch" "$M_ARCHS"; then
            echo "$M_DIR"; continue
        fi
    done
}

# Echo dirs that should be downloaded for the given arch (core + optional).
# Used by setup_prebuilt.sh for reporting; actual download logic stays in
# the setup_model_* functions.
models_download_dirs_for_arch() {
    local arch="$1"
    local entry
    for entry in "${MODEL_MANIFEST[@]}"; do
        _parse_entry "$entry" || continue
        case "$M_CLASS" in
            core|optional) ;;
            *) continue ;;
        esac
        _list_contains "$arch" "$M_ARCHS" || continue
        echo "$M_DIR"
    done
}

# Echo core dirs for the given (device, arch) — used to verify packaging
# includes everything the device actually needs.
models_core_dirs_for() {
    local device="$1" arch="$2"
    local entry
    for entry in "${MODEL_MANIFEST[@]}"; do
        _parse_entry "$entry" || continue
        [[ "$M_CLASS" == "core" ]] || continue
        _list_contains "$device" "$M_DEVICES" || continue
        _list_contains "$arch" "$M_ARCHS" || continue
        echo "$M_DIR"
    done
}
