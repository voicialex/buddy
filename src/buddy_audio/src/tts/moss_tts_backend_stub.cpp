#include "buddy_audio/tts/tts_backend.hpp"

#include <cstdio>

// Stub when MOSS-TTS dependencies (onnxruntime-gpu, sentencepiece) are not available
std::unique_ptr<TtsBackend> create_moss_tts_backend() {
    fprintf(stderr, "[WARN] MOSS-TTS not available: built without onnxruntime-gpu/sentencepiece.\n"
                    "       Run: ./scripts/setup_prebuilt.sh moss-tts && ./build.sh -c\n");
    return nullptr;
}
