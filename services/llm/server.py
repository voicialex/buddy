#!/usr/bin/env python3
"""Unified LLM Service for buddy robot.

Provides SSE streaming interface for all LLM inference modes.
Replaces C++ buddy_local_llm + buddy_cloud nodes.
"""
import asyncio
import json
import logging
import os
import sys
from pathlib import Path

import uvicorn
import yaml
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import StreamingResponse

sys.path.insert(0, str(Path(__file__).parent))

from backends.ollama import OllamaBackend
from backends.openai_compat import OpenAICompatBackend
from backends.rk_llm import RkLlmBackend
from backends.vllm import VLLMBackend
from memory import MemoryManager, MemoryStore, Summarizer
from router import Router

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("llm_server")

app = FastAPI(title="Buddy LLM Service")

router: Router | None = None
memory_manager: MemoryManager | None = None
config: dict = {}
VALID_LLM_MODES = {"local_only", "cloud_only", "local_route"}


def load_config() -> dict:
    config_path = Path(__file__).parent / "config.yaml"
    if config_path.exists():
        with open(config_path) as f:
            return yaml.safe_load(f)
    return {}


def create_backend(cfg: dict):
    backend_type = cfg.get("backend", "")
    if backend_type == "ollama":
        return OllamaBackend(
            url=cfg.get("url", "http://localhost:11434"),
            model=cfg.get("model", "qwen2.5:7b"),
            keep_alive=cfg.get("keep_alive", "30m"),
            request_timeout_sec=float(cfg.get("request_timeout_sec", 120)),
        )
    elif backend_type == "openai_compat":
        return OpenAICompatBackend(
            base_url=cfg.get("base_url", ""),
            model=cfg.get("model", ""),
            api_key_env=cfg.get("api_key_env", ""),
            api_key=cfg.get("api_key", ""),
        )
    elif backend_type == "vllm":
        return VLLMBackend(
            base_url=cfg.get("base_url", "http://localhost:8000"),
            model=cfg.get("model", "Qwen/Qwen3-4B"),
            api_key=cfg.get("api_key", "EMPTY"),
            enable_thinking=cfg.get("enable_thinking"),
        )
    elif backend_type == "rk_llm":
        project_root = Path(__file__).resolve().parents[2]
        default_start_cmd = f"\"{project_root}/scripts/start_llm_server.sh\" start-rkllm"
        default_stop_cmd = f"\"{project_root}/scripts/start_llm_server.sh\" stop-rkllm"
        return RkLlmBackend(
            base_url=cfg.get("base_url", "http://127.0.0.1:8080"),
            model=cfg.get("model", "buddy"),
            api_key=cfg.get("api_key", ""),
            endpoint=cfg.get("endpoint", ""),
            api_style=cfg.get("api_style", "rkllm_demo"),
            autostart=bool(cfg.get("autostart", True)),
            autostart_cmd=cfg.get("autostart_cmd", default_start_cmd),
            stop_after_request=bool(cfg.get("stop_after_request", False)),
            stop_cmd=cfg.get("stop_cmd", default_stop_cmd),
            stop_idle_sec=int(cfg.get("stop_idle_sec", 60)),
            autostart_timeout_sec=int(cfg.get("autostart_timeout_sec", 120)),
        )
    return None


def resolve_active_backend(local_cfg: dict) -> str:
    requested = local_cfg.get("active_backend", "ollama")
    backends = local_cfg.get("backends", {})

    # Highest priority: explicit env override from deployment/runtime.
    env_backend = os.getenv("BUDDY_LLM_BACKEND", "").strip()
    if env_backend:
        return env_backend

    if requested != "auto":
        return requested

    arch = os.getenv("BUDDY_TARGET_ARCH", "").strip().lower()
    device = os.getenv("BUDDY_TARGET_DEVICE", "").strip().lower()
    if arch in ("arm64", "aarch64") and device == "npu" and "rk_llm" in backends:
        return "rk_llm"

    if "ollama" in backends:
        return "ollama"
    if "vllm" in backends:
        return "vllm"
    return next(iter(backends), "")


@app.on_event("startup")
async def startup():
    global router, memory_manager, config
    config = load_config()
    configured_mode = str(config.get("mode", "local_route")).strip()
    if configured_mode not in VALID_LLM_MODES:
        raise RuntimeError(
            f"Invalid config.mode='{configured_mode}', allowed: {', '.join(sorted(VALID_LLM_MODES))}"
        )

    local_cfg = config.get("local", {})
    active = resolve_active_backend(local_cfg)
    backend_cfg = local_cfg.get("backends", {}).get(active, {})
    cloud_cfg = config.get("cloud", {})

    local_backend = create_backend(backend_cfg) if backend_cfg else None
    cloud_backend = None
    try:
        cloud_backend = create_backend(cloud_cfg) if cloud_cfg else None
    except ValueError as e:
        logger.warning(f"Cloud backend disabled: {e}")

    router = Router(local=local_backend, cloud=cloud_backend)

    # Initialize memory system
    memory_cfg = config.get("memory", {})
    if memory_cfg.get("enabled", False) and cloud_backend:
        data_dir = Path(__file__).parent / memory_cfg.get("data_dir", "data/memory")
        store = MemoryStore(data_dir=data_dir)
        summarizer = Summarizer(cloud_backend=cloud_backend)
        memory_manager = MemoryManager(store=store, summarizer=summarizer, config=memory_cfg)
        logger.info(f"[MEMORY] enabled, data_dir={data_dir}")
    else:
        logger.info("[MEMORY] disabled (memory.enabled=false or no cloud backend)")

    logger.info(
        f"LLM service ready: local={type(local_backend).__name__ if local_backend else 'None'}({active}), "
        f"cloud={type(cloud_backend).__name__ if cloud_backend else 'None'}"
    )


@app.post("/v1/chat")
async def chat(request: Request):
    body = await request.json()
    messages = body.get("messages", [])
    mode = body.get("mode", config.get("mode", "local_route"))
    mode = str(mode).strip()
    if mode not in VALID_LLM_MODES:
        raise HTTPException(
            status_code=400,
            detail=f"Invalid mode='{mode}', allowed: {', '.join(sorted(VALID_LLM_MODES))}",
        )
    session_id = body.get("session_id", "default")
    emotion = body.get("emotion", "")
    image_base64 = body.get("image_base64", "")
    user_text = messages[-1].get("content", "") if messages else ""

    logger.info(f"[REQ] mode={mode}, session={session_id}, emotion={emotion}, has_image={bool(image_base64)}, user={user_text[:60]}")

    # Enhance messages with memory context
    if memory_manager:
        enhanced_messages = await memory_manager.build_context(session_id, messages)
    else:
        enhanced_messages = messages

    local_prompt = config.get("local", {}).get("system_prompt", "")
    cloud_prompt = config.get("cloud", {}).get("system_prompt", "")
    rules = config.get("rules", "")
    if rules:
        local_prompt = f"{local_prompt}\n{rules}" if local_prompt else rules
        cloud_prompt = f"{cloud_prompt}\n{rules}" if cloud_prompt else rules

    # Inject emotion context into prompts
    if emotion:
        emotion_hint = f"\n[用户当前情绪: {emotion}]"
        local_prompt += emotion_hint
        cloud_prompt += emotion_hint

    routing_prompt = config.get("local", {}).get("routing_prompt", "")

    async def event_stream():
        full_response = []
        async for chunk in router.route(enhanced_messages, mode, local_prompt, cloud_prompt, routing_prompt, image_base64):
            if chunk.text:
                full_response.append(chunk.text)
            data = json.dumps(
                {"text": chunk.text, "source": chunk.source, "model": chunk.model, "done": chunk.done},
                ensure_ascii=False,
                separators=(",", ":"),
            )
            yield f"data: {data}\n\n"

        # Save turn to memory after response completes
        if memory_manager and full_response:
            assistant_text = "".join(full_response)
            asyncio.create_task(
                memory_manager.save_turn(session_id, user_text, assistant_text)
            )

    return StreamingResponse(event_stream(), media_type="text/event-stream")


@app.post("/v1/session/end")
async def session_end(request: Request):
    """Endpoint for C++ brain to signal session end."""
    body = await request.json()
    session_id = body.get("session_id", "")
    if memory_manager and session_id:
        asyncio.create_task(memory_manager.on_session_end(session_id))
        return {"status": "ok", "session_id": session_id}
    return {"status": "noop"}


@app.get("/health")
async def health():
    local_ok = await router.local.health_check() if router and router.local else False
    cloud_ok = await router.cloud.health_check() if router and router.cloud else False
    return {"local": local_ok, "cloud": cloud_ok, "memory": memory_manager is not None}

@app.get("/ready")
async def ready():
    # Liveness-only endpoint for startup scripts. Do not block on backend health checks.
    return {"ready": True}


if __name__ == "__main__":
    cfg = load_config()
    server_cfg = cfg.get("server", {})
    uvicorn.run(
        "server:app",
        host=server_cfg.get("host", "127.0.0.1"),
        port=server_cfg.get("port", 8002),
        log_level="info",
    )
