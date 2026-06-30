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


# 关键词规则优先于 LLM 路由决策，避免每轮都付 8s 路由延迟。
# 命中即定 LOCAL/CLOUD；未命中再走 LLM 判定。
CLOUD_KEYWORDS = (
    "天气", "新闻", "股价", "股票", "比分", "赛事", "汇率",
    "today", "weather", "news", "stock", "price",
)
LOCAL_KEYWORDS = (
    "你好", "早上好", "晚上好", "再见", "谢谢", "你是谁",
    "hello", "hi", "hey", "bye", "thanks",
)


def rule_based_decision(user_text: str) -> str | None:
    """Return 'LOCAL'/'CLOUD' if a keyword rule matches, else None."""
    text = user_text.strip().lower()
    if not text:
        return None
    if any(k in text for k in CLOUD_KEYWORDS):
        return "CLOUD"
    if any(k in text for k in LOCAL_KEYWORDS):
        return "LOCAL"
    return None


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

                # 1) 关键词规则前置：命中即定路由，跳过 LLM 决策延迟
                user_text = messages[-1].get("content", "") if messages else ""
                decision = rule_based_decision(user_text)
                if decision:
                    logger.info(f"[ROUTE] rule-based decision: {decision}")
                else:
                    # 2) 无 cloud backend：跳过决策直接本地
                    decision = "LOCAL"
                    if self.cloud:
                        t0 = time.monotonic()
                        try:
                            decision_response = await asyncio.wait_for(
                                self.local.complete(messages, routing_prompt),
                                timeout=2.0,
                            )
                            decision = parse_decision(decision_response)
                            decision_ms = int((time.monotonic() - t0) * 1000)
                            logger.info(f"[ROUTE] {'='*40}")
                            logger.info(f"[ROUTE] DECISION: {decision} ({decision_ms}ms)")
                            logger.info(f"[ROUTE] {'='*40}")
                        except asyncio.TimeoutError:
                            logger.warning("[ROUTE] decision timeout after 2s, fallback LOCAL")
                            decision = "LOCAL"
                        except Exception as e:
                            logger.warning(f"[ROUTE] decision failed, fallback LOCAL: {e}")
                            decision = "LOCAL"
                    else:
                        logger.info("[ROUTE] cloud backend unavailable, bypass decision and use LOCAL")

                if decision == "CLOUD" and self.cloud:
                    t2 = time.monotonic()
                    try:
                        async for chunk in self.cloud.stream_chat(messages, cloud_system_prompt, image_base64=image_base64):
                            yield StreamChunk(text=chunk, source="cloud", done=False, model=cloud_model)
                        cloud_ms = int((time.monotonic() - t2) * 1000)
                        logger.info(f"[DONE] → cloud, model={cloud_model}, time={cloud_ms}ms")
                    except Exception as e:
                        logger.error(f"[ROUTE] cloud stream error: {e}")
                        yield StreamChunk(text="Cloud service unavailable", source="cloud", done=True, model=cloud_model)
                        return
                    yield StreamChunk(text="", source="cloud", done=True, model=cloud_model)
                else:
                    if decision == "CLOUD" and not self.cloud:
                        logger.warning("[ROUTE] decision=CLOUD but cloud not configured")
                    t1 = time.monotonic()
                    async for chunk in self.local.stream_chat(messages, local_system_prompt):
                        yield StreamChunk(text=chunk, source="local", done=False, model=local_model)
                    local_gen_ms = int((time.monotonic() - t1) * 1000)
                    logger.info(f"[DONE] → local, model={local_model}, time={local_gen_ms}ms")
                    yield StreamChunk(text="", source="local", done=True, model=local_model)

            case _:
                yield StreamChunk(text=f"Unknown mode: {mode}", source="local", done=True)
