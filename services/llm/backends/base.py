from abc import ABC, abstractmethod
from typing import AsyncIterator


class LLMBackend(ABC):
    """Abstract interface for LLM inference backends."""

    @abstractmethod
    async def stream_chat(
        self,
        messages: list[dict],
        system_prompt: str = "",
        image_base64: str = "",
    ) -> AsyncIterator[str]:
        """Yield text chunks as they arrive."""
        ...

    @abstractmethod
    async def complete(
        self,
        messages: list[dict],
        system_prompt: str = "",
    ) -> str:
        """Return full response (non-streaming). Used by local_route for decision parsing."""
        ...

    @abstractmethod
    async def health_check(self) -> bool:
        ...

    async def warmup(self) -> None:
        """Pre-warm the backend (e.g. start managed services). Called at startup."""
        return
