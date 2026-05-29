import httpx
import json
from typing import AsyncIterator

from .base import LLMBackend


class OllamaBackend(LLMBackend):
    def __init__(self, url: str = "http://localhost:11434", model: str = "buddy"):
        self.url = url.rstrip("/")
        self.model = model

    async def stream_chat(
        self,
        messages: list[dict],
        system_prompt: str = "",
        image_base64: str = "",
    ) -> AsyncIterator[str]:
        payload = self._build_payload(messages, system_prompt, stream=True)
        async with httpx.AsyncClient(timeout=60.0) as client:
            async with client.stream("POST", f"{self.url}/api/chat", json=payload) as resp:
                resp.raise_for_status()
                async for line in resp.aiter_lines():
                    if not line:
                        continue
                    data = json.loads(line)
                    content = data.get("message", {}).get("content", "")
                    if content:
                        yield content

    async def complete(
        self,
        messages: list[dict],
        system_prompt: str = "",
    ) -> str:
        payload = self._build_payload(messages, system_prompt, stream=False)
        async with httpx.AsyncClient(timeout=60.0) as client:
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

    def _build_payload(self, messages: list[dict], system_prompt: str, stream: bool) -> dict:
        msgs = []
        if system_prompt:
            msgs.append({"role": "system", "content": system_prompt})
        msgs.extend(messages)
        return {"model": self.model, "messages": msgs, "stream": stream}
