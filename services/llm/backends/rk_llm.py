import json
from typing import AsyncIterator

import httpx

from .base import LLMBackend


class RkLlmBackend(LLMBackend):
    """RK-LLM backend.

    Supports two wire protocols:
    - rkllm_demo: /rkllm_chat (flask demo, plain JSON lines when stream=True)
    - openai: /v1/chat/completions (SSE with data: prefix)
    """

    def __init__(
        self,
        base_url: str = "http://127.0.0.1:8080",
        model: str = "buddy",
        api_key: str = "",
        endpoint: str = "",
        api_style: str = "rkllm_demo",
    ):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.api_key = api_key
        self.api_style = (api_style or "rkllm_demo").strip().lower()
        self.endpoint = self._normalize_endpoint(endpoint) if endpoint else self._default_endpoint()

    def _default_endpoint(self) -> str:
        if self.api_style == "openai":
            return "/v1/chat/completions"
        return "/rkllm_chat"

    @staticmethod
    def _normalize_endpoint(endpoint: str) -> str:
        return endpoint if endpoint.startswith("/") else f"/{endpoint}"

    def _url(self) -> str:
        return f"{self.base_url}{self.endpoint}"

    def _headers(self) -> dict:
        headers = {"Content-Type": "application/json"}
        if self.api_key:
            headers["Authorization"] = f"Bearer {self.api_key}"
        return headers

    def _build_request_body(
        self,
        messages: list[dict],
        system_prompt: str,
        stream: bool,
    ) -> dict:
        msgs = []
        if system_prompt:
            msgs.append({"role": "system", "content": system_prompt})
        msgs.extend(messages)
        return {"model": self.model, "messages": msgs, "stream": stream}

    @staticmethod
    def _extract_stream_text(data: dict) -> str:
        choices = data.get("choices") or []
        if not choices:
            return ""
        choice = choices[-1]
        delta = choice.get("delta") or {}
        if delta.get("content"):
            return delta["content"]
        message = choice.get("message") or {}
        return message.get("content", "") or ""

    @staticmethod
    def _extract_complete_text(data: dict) -> str:
        choices = data.get("choices") or []
        if not choices:
            return ""
        message = choices[-1].get("message") or {}
        if message.get("content"):
            return message["content"]
        delta = choices[-1].get("delta") or {}
        return delta.get("content", "") or ""

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
                self._url(),
                json=body,
                headers=self._headers(),
            ) as resp:
                resp.raise_for_status()
                async for line in resp.aiter_lines():
                    if not line:
                        continue
                    data_str = line.strip()

                    # OpenAI SSE style: "data: {...}" / "data: [DONE]"
                    if data_str.startswith("data: "):
                        data_str = data_str[6:].strip()
                        if data_str == "[DONE]":
                            break

                    # rkllm demo returns plain lines; ignore separators safely.
                    if data_str in ("", "[DONE]"):
                        continue

                    try:
                        data = json.loads(data_str)
                    except json.JSONDecodeError:
                        continue

                    content = self._extract_stream_text(data)
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
                self._url(),
                json=body,
                headers=self._headers(),
            )
            resp.raise_for_status()
            data = resp.json()
            return self._extract_complete_text(data)

    async def health_check(self) -> bool:
        try:
            async with httpx.AsyncClient(timeout=5.0) as client:
                resp = await client.get(f"{self.base_url}/health")
                if resp.status_code == 200:
                    return True
                resp = await client.get(f"{self.base_url}/v1/models")
                if resp.status_code == 200:
                    return True

                # Fallback for rkllm demo service without health/models endpoints.
                probe = {
                    "model": self.model,
                    "messages": [{"role": "user", "content": "ping"}],
                    "stream": False,
                }
                resp = await client.post(self._url(), json=probe, headers=self._headers())
                return resp.status_code == 200
        except Exception:
            return False
