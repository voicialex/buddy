"""LLM routing logic for unified service."""
import asyncio
import logging
import time
from dataclasses import dataclass
from typing import AsyncIterator

from backends.base import LLMBackend

logger = logging.getLogger("llm_server")


@dataclass
class StreamChunk:
    text: str
    source: str  # "local" | "cloud"
    done: bool
    model: str = ""


def parse_decision(response: str) -> str:
    """Parse LOCAL/CLOUD decision from LLM response."""
    first_line = response.strip().split("\n", 1)[0].strip().upper()
    if "CLOUD" in first_line:
        return "CLOUD"
    return "LOCAL"


class Router:
    def __init__(self, local: LLMBackend | None, cloud: LLMBackend | None):
        self.local = local
        self.cloud = cloud

    async def route(
        self,
        messages: list[dict],
        mode: str,
        local_system_prompt: str = "",
        cloud_system_prompt: str = "",
        routing_prompt: str = "",
        image_base64: str = "",
    ) -> AsyncIterator[StreamChunk]:
        local_model = getattr(self.local, "model", "local") if self.local else ""
        cloud_model = getattr(self.cloud, "model", "cloud") if self.cloud else ""

        match mode:
            case "local_only":
                if not self.local:
                    yield StreamChunk(text="Local backend not configured", source="local", done=True)
                    return
                async for chunk in self.local.stream_chat(messages, local_system_prompt):
                    yield StreamChunk(text=chunk, source="local", done=False, model=local_model)
                yield StreamChunk(text="", source="local", done=True, model=local_model)

            case "cloud_only":
                if not self.cloud:
                    yield StreamChunk(text="Cloud backend not configured", source="cloud", done=True)
                    return
                async for chunk in self.cloud.stream_chat(messages, cloud_system_prompt, image_base64=image_base64):
                    yield StreamChunk(text=chunk, source="cloud", done=False, model=cloud_model)
                yield StreamChunk(text="", source="cloud", done=True, model=cloud_model)

            case "local_route":
                if not self.local:
                    yield StreamChunk(text="Local backend not configured", source="local", done=True)
                    return

                # No cloud backend: skip route decision and answer locally immediately.
                # This avoids long "decision" RTT on slow local models (e.g. Ollama on CPU).
                decision = "LOCAL"
                if self.cloud:
                    t0 = time.time()
                    try:
                        decision_response = await asyncio.wait_for(
                            self.local.complete(messages, routing_prompt),
                            timeout=8.0,
                        )
                        decision = parse_decision(decision_response)
                        decision_ms = int((time.time() - t0) * 1000)
                        logger.info(f"[ROUTE] {'='*40}")
                        logger.info(f"[ROUTE] DECISION: {decision} ({decision_ms}ms)")
                        logger.info(f"[ROUTE] {'='*40}")
                    except Exception as e:
                        logger.warning(f"[ROUTE] decision failed, fallback LOCAL: {e}")
                        decision = "LOCAL"
                else:
                    logger.info("[ROUTE] cloud backend unavailable, bypass decision and use LOCAL")

                # Step 2: Always stream local reply first (fills the gap)
                t1 = time.time()
                local_text = []
                async for chunk in self.local.stream_chat(messages, local_system_prompt):
                    local_text.append(chunk)
                    yield StreamChunk(text=chunk, source="local", done=False, model=local_model)
                local_gen_ms = int((time.time() - t1) * 1000)
                logger.info(f"[ROUTE] local({local_model}): {''.join(local_text)[:120]!r}")
                logger.info(f"[DONE] → local, model={local_model}, time={local_gen_ms}ms")

                # Step 3: If CLOUD, append cloud response after local
                if decision == "CLOUD" and self.cloud:
                    # Signal local portion done (not final done)
                    yield StreamChunk(text="", source="local", done=False, model=local_model)
                    t2 = time.time()
                    cloud_text = []
                    try:
                        async for chunk in self.cloud.stream_chat(messages, cloud_system_prompt, image_base64=image_base64):
                            cloud_text.append(chunk)
                            yield StreamChunk(text=chunk, source="cloud", done=False, model=cloud_model)
                        cloud_ms = int((time.time() - t2) * 1000)
                        logger.info(f"[ROUTE] cloud({cloud_model}): {''.join(cloud_text)[:120]!r}")
                        logger.info(f"[DONE] → cloud, model={cloud_model}, time={cloud_ms}ms")
                        yield StreamChunk(text="", source="cloud", done=True, model=cloud_model)
                    except Exception as e:
                        logger.error(f"[ROUTE] cloud error: {e}, using local reply only")
                        yield StreamChunk(text="", source="local", done=True, model=local_model)
                else:
                    if decision == "CLOUD" and not self.cloud:
                        logger.warning("[ROUTE] decision=CLOUD but cloud not configured")
                    yield StreamChunk(text="", source="local", done=True, model=local_model)

            case _:
                yield StreamChunk(text=f"Unknown mode: {mode}", source="local", done=True)
