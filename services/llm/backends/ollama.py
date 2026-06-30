import httpx
import json
from typing import AsyncIterator

from .base import LLMBackend


class OllamaBackend(LLMBackend):
    def __init__(
        self,
        url: str = "http://localhost:11434",
        model: str = "qwen2.5:7b",
        keep_alive: str = "30m",
        request_timeout_sec: float = 120.0,
    ):
        self.url = url.rstrip("/")
        self.model = model
        self.keep_alive = keep_alive
        self.request_timeout_sec = max(10.0, float(request_timeout_sec))
        self._supports_thinking: bool | None = None

    async def _check_thinking_support(self) -> bool:
        if self._supports_thinking is not None:
            return self._supports_thinking
        try:
            async with httpx.AsyncClient(timeout=5.0) as client:
                resp = await client.post(f"{self.url}/api/show", json={"name": self.model})
                resp.raise_for_status()
                data = resp.json()
                self._supports_thinking = "IsThinkSet" in data.get("template", "")
        except Exception:
            # Don't cache failures permanently — retry next time
            return False
        return self._supports_thinking

    async def stream_chat(
        self,
        messages: list[dict],
        system_prompt: str = "",
        image_base64: str = "",
    ) -> AsyncIterator[str]:
        payload = await self._build_payload(messages, system_prompt, stream=True)
        async with httpx.AsyncClient(timeout=self.request_timeout_sec) as client:
            async with client.stream("POST", f"{self.url}/api/chat", json=payload) as resp:
                resp.raise_for_status()
                async for line in resp.aiter_lines():
                    if not line:
                        continue
                    try:
                        data = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    content = data.get("message", {}).get("content", "")
                    if content:
                        yield content

    async def complete(
        self,
        messages: list[dict],
        system_prompt: str = "",
    ) -> str:
        payload = await self._build_payload(messages, system_prompt, stream=False)
        async with httpx.AsyncClient(timeout=self.request_timeout_sec) as client:
            resp = await client.post(f"{self.url}/api/chat", json=payload)
            resp.raise_for_status()
            data = resp.json()
            return data.get("message", {}).get("content", "")

    async def health_check(self) -> bool:
        try:
            async with httpx.AsyncClient(timeout=5.0) as client:
                resp = await client.get(self.url)
                return resp.status_code == 200
        except Exception:
            return False

    async def _build_payload(self, messages: list[dict], system_prompt: str, stream: bool) -> dict:
        msgs = []
        if system_prompt:
            msgs.append({"role": "system", "content": system_prompt})
        msgs.extend(messages)
        payload = {
            "model": self.model,
            "messages": msgs,
            "stream": stream,
            "keep_alive": self.keep_alive,
        }
        if await self._check_thinking_support():
            payload["think"] = True
        return payload
