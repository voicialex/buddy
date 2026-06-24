import asyncio
import json
import logging
import subprocess
import time
from typing import AsyncIterator

import httpx

from .base import LLMBackend

logger = logging.getLogger("rk_llm")


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
        autostart: bool = False,
        autostart_cmd: str = "",
        stop_after_request: bool = False,
        stop_cmd: str = "",
        stop_idle_sec: int = 60,
        autostart_timeout_sec: int = 120,
    ):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.api_key = api_key
        self.api_style = (api_style or "rkllm_demo").strip().lower()
        self.endpoint = self._normalize_endpoint(endpoint) if endpoint else self._default_endpoint()
        self.autostart = autostart
        self.autostart_cmd = autostart_cmd.strip()
        self.stop_after_request = stop_after_request
        self.stop_cmd = stop_cmd.strip()
        self.stop_idle_sec = max(0, int(stop_idle_sec))
        self.autostart_timeout_sec = max(10, int(autostart_timeout_sec))
        self._startup_lock = asyncio.Lock()
        self._request_lock = asyncio.Lock()
        self._active_requests = 0
        self._idle_stop_task: asyncio.Task | None = None

    def _default_endpoint(self) -> str:
        if self.api_style == "openai":
            return "/v1/chat/completions"
        return "/rkllm_chat"

    @staticmethod
    def _normalize_endpoint(endpoint: str) -> str:
        return endpoint if endpoint.startswith("/") else f"/{endpoint}"

    def _url(self) -> str:
        return f"{self.base_url}{self.endpoint}"

    async def _run_shell(self, cmd: str) -> None:
        proc = await asyncio.to_thread(
            subprocess.run,
            ["bash", "-lc", cmd],
            check=False,
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            logger.error("Shell command failed (rc=%d): %s", proc.returncode, cmd)
            if proc.stderr:
                logger.error("stderr: %s", proc.stderr.strip())

    async def _is_service_ready(self) -> bool:
        try:
            async with httpx.AsyncClient(timeout=3.0) as client:
                # Lightweight probes first; avoid triggering model inference unless needed.
                resp = await client.get(f"{self.base_url}/health")
                if resp.status_code == 200:
                    return True
                resp = await client.get(f"{self.base_url}/v1/models")
                if resp.status_code == 200:
                    return True

                probe = {
                    "model": self.model,
                    "messages": [{"role": "user", "content": "ping"}],
                    "stream": False,
                }
                resp = await client.post(self._url(), json=probe, headers=self._headers())
                return resp.status_code in (200, 503)
        except Exception:
            return False

    async def _ensure_ready(self) -> None:
        if await self._is_service_ready():
            return
        if not self.autostart or not self.autostart_cmd:
            raise RuntimeError("RKLLM service is not ready")

        async with self._startup_lock:
            if await self._is_service_ready():
                return
            logger.info("Autostarting RKLLM server: %s", self.autostart_cmd)
            await self._run_shell(self.autostart_cmd)
            deadline = time.monotonic() + self.autostart_timeout_sec
            while time.monotonic() < deadline:
                if await self._is_service_ready():
                    logger.info("RKLLM server ready")
                    return
                await asyncio.sleep(0.8)
            raise RuntimeError("RKLLM service autostart timeout")

    async def _cancel_idle_stop_task(self) -> None:
        if self._idle_stop_task and not self._idle_stop_task.done():
            self._idle_stop_task.cancel()
            try:
                await self._idle_stop_task
            except asyncio.CancelledError:
                pass
        self._idle_stop_task = None

    async def _on_request_start(self) -> None:
        async with self._request_lock:
            self._active_requests += 1
        await self._cancel_idle_stop_task()

    async def _idle_stop_worker(self) -> None:
        try:
            await asyncio.sleep(self.stop_idle_sec)
            async with self._request_lock:
                if self._active_requests > 0:
                    return
            await self._run_shell(self.stop_cmd)
        except asyncio.CancelledError:
            return

    async def _on_request_end(self) -> None:
        immediate_stop = False
        async with self._request_lock:
            if self._active_requests > 0:
                self._active_requests -= 1
            if self._active_requests > 0:
                return
            if not self.stop_after_request or not self.stop_cmd:
                return
            if self.stop_idle_sec <= 0:
                immediate_stop = True
            else:
                if self._idle_stop_task and not self._idle_stop_task.done():
                    self._idle_stop_task.cancel()
                self._idle_stop_task = asyncio.create_task(self._idle_stop_worker())
        if immediate_stop:
            await self._run_shell(self.stop_cmd)

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
        await self._on_request_start()
        try:
            await self._ensure_ready()
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
        finally:
            await self._on_request_end()

    async def complete(
        self,
        messages: list[dict],
        system_prompt: str = "",
    ) -> str:
        await self._on_request_start()
        try:
            await self._ensure_ready()
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
        finally:
            await self._on_request_end()

    async def health_check(self) -> bool:
        return await self._is_service_ready()

    async def warmup(self) -> None:
        """Pre-start RKLLM server in the background so the first request is fast."""
        if not self.autostart or not self.autostart_cmd:
            return
        if await self._is_service_ready():
            return
        logger.info("Warming up RKLLM server in background...")
        asyncio.create_task(self._ensure_ready())
