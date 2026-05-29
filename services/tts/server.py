#!/usr/bin/env python3
"""ChatTTS HTTP server for buddy robot.

Loads the ChatTTS model and exposes a POST /tts endpoint that accepts
JSON {"text": "..."} and returns raw float32 PCM audio at 16kHz.

Usage:
    pip install -r services/tts/requirements.txt
    python services/tts/server.py
"""
import io
import os
import struct

import ChatTTS
import numpy as np
import torch
from fastapi import FastAPI, Request
from fastapi.responses import Response
from scipy.signal import resample

app = FastAPI(title="ChatTTS Server")

# Model path: env var CHAT_TTS_MODELS or default to src/buddy_audio/models/ChatTTS
_project_root = os.path.join(os.path.dirname(__file__), "..", "..")
_model_dir = os.environ.get(
    "CHAT_TTS_MODELS",
    os.path.join(_project_root, "src", "buddy_audio", "models", "ChatTTS"),
)
os.makedirs(_model_dir, exist_ok=True)

chat = ChatTTS.Chat()
chat.load(compile=False, custom_path=_model_dir)

if not hasattr(chat, "speaker") or chat.speaker is None:
    raise RuntimeError(
        "ChatTTS model failed to load. Check network access to huggingface.co.\n"
        "If behind a firewall, try: export HF_ENDPOINT=https://hf-mirror.com"
    )

# Sample a fixed speaker embedding for consistent voice across requests
spk = chat.sample_random_speaker()


@app.post("/tts")
async def tts(request: Request):
    body = await request.json()
    text = body.get("text", "")
    if not text:
        return Response(content=b"", media_type="audio/pcm")

    params = ChatTTS.Chat.InferCodeParams(
        spk_emb=spk,
        temperature=0.3,
    )
    wavs = chat.infer([text], params_infer_code=params)

    # ChatTTS outputs 24kHz float32 audio
    audio = np.array(wavs[0], dtype=np.float32).flatten()

    # Resample 24kHz -> 16kHz to match buddy project sample_rate
    target_len = int(len(audio) * 16000 / 24000)
    audio_16k = resample(audio, target_len).astype(np.float32)

    pcm_bytes = audio_16k.tobytes()
    return Response(
        content=pcm_bytes,
        media_type="audio/pcm",
        headers={
            "X-Sample-Rate": "16000",
            "X-Samples": str(len(audio_16k)),
        },
    )


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="127.0.0.1", port=9880)
