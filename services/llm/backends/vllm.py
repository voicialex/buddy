import httpx
import json
from typing import AsyncIterator

from .base import LLMBackend


class VLLMBackend(LLMBackend):
    """vLLM backend with OpenAI-compatible API.

    Supports qwen3 thinking mode control via chat_template_kwargs.
    """

    def __init__(
        self,
        base_url: str = "http://localhost:8000",
        model: str = "Qwen/Qwen3-4B",
        api_key: str = "EMPTY",
        enable_thinking: bool | None = None,
    ):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.api_key = api_key
        self.enable_thinking = enable_thinking

    def _build_request_body(
        self, messages: list[dict], system_prompt: str, stream: bool
    ) -> dict:
        msgs = []
        if system_prompt:
            msgs.append({"role": "system", "content": system_prompt})
        msgs.extend(messages)

        body = {
            "model": self.model,
            "messages": msgs,
            "stream": stream,
        }

        if self.enable_thinking is not None:
            body["chat_template_kwargs"] = {"enable_thinking": self.enable_thinking}

        return body

    def _headers(self) -> dict:
        return {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }

    async def stream_chat(
        self,
        messages: list[dict],
        system_prompt: str = "",
        image_base64: str = "",
    ) -> AsyncIterator[str]:
        body = self._build_request_body(messages, system_prompt, stream=True)
        async with httpx.AsyncClient(timeout=60.0) as client:
            async with client.stream(
                "POST",
                f"{self.base_url}/v1/chat/completions",
                json=body,
                headers=self._headers(),
            ) as resp:
                resp.raise_for_status()
                async for line in resp.aiter_lines():
                    if not line or not line.startswith("data: "):
                        continue
                    data_str = line[6:]
                    if data_str.strip() == "[DONE]":
                        break
                    data = json.loads(data_str)
                    delta = data["choices"][0].get("delta", {}) if data.get("choices") else {}
                    content = delta.get("content", "")
                    if content:
                        yield content

    async def complete(
        self,
        messages: list[dict],
        system_prompt: str = "",
    ) -> str:
        body = self._build_request_body(messages, system_prompt, stream=False)
        async with httpx.AsyncClient(timeout=60.0) as client:
            resp = await client.post(
                f"{self.base_url}/v1/chat/completions",
                json=body,
                headers=self._headers(),
            )
            resp.raise_for_status()
            data = resp.json()
            return data["choices"][0]["message"]["content"] or ""

    async def health_check(self) -> bool:
        try:
            async with httpx.AsyncClient(timeout=5.0) as client:
                resp = await client.get(f"{self.base_url}/v1/models")
                return resp.status_code == 200
        except Exception:
            return False
