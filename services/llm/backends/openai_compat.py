import os
from typing import AsyncIterator

import httpx
from openai import AsyncOpenAI

from .base import LLMBackend


class OpenAICompatBackend(LLMBackend):
    def __init__(
        self,
        base_url: str,
        model: str,
        api_key: str = "",
        api_key_env: str = "",
        request_timeout_sec: float = 120.0,
    ):
        key = api_key or os.environ.get(api_key_env, "")
        if not key:
            raise ValueError(f"No API key: set {api_key_env} env or provide api_key")
        timeout = httpx.Timeout(connect=5.0, read=float(request_timeout_sec), write=10.0, pool=5.0)
        self.client = AsyncOpenAI(base_url=base_url, api_key=key, timeout=timeout)
        self.model = model

    async def stream_chat(
        self,
        messages: list[dict],
        system_prompt: str = "",
        image_base64: str = "",
    ) -> AsyncIterator[str]:
        msgs = []
        if system_prompt:
            msgs.append({"role": "system", "content": system_prompt})
        msgs.extend(messages)

        # Convert last user message to OpenAI vision format when image is present
        if image_base64 and msgs:
            for i in range(len(msgs) - 1, -1, -1):
                if msgs[i].get("role") == "user":
                    text = msgs[i]["content"] if isinstance(msgs[i]["content"], str) else ""
                    msgs[i] = {
                        "role": "user",
                        "content": [
                            {"type": "text", "text": text},
                            {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{image_base64}"}},
                        ],
                    }
                    break

        stream = await self.client.chat.completions.create(
            model=self.model, messages=msgs, stream=True
        )
        async for chunk in stream:
            delta = chunk.choices[0].delta if chunk.choices else None
            if delta and delta.content:
                yield delta.content

    async def complete(
        self,
        messages: list[dict],
        system_prompt: str = "",
    ) -> str:
        msgs = []
        if system_prompt:
            msgs.append({"role": "system", "content": system_prompt})
        msgs.extend(messages)
        resp = await self.client.chat.completions.create(
            model=self.model, messages=msgs, stream=False
        )
        return resp.choices[0].message.content or ""

    async def health_check(self) -> bool:
        try:
            await self.client.models.list()
            return True
        except Exception:
            return False
