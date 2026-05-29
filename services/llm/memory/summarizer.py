"""Summarizer — calls cloud LLM for summarization and fact extraction."""
import json
import logging
from datetime import datetime, timezone

from backends.base import LLMBackend
from .store import Fact
from .prompts import SUMMARIZE_PROMPT, EXTRACT_FACTS_PROMPT, DEDUPE_FACTS_PROMPT

logger = logging.getLogger("llm_server")


class Summarizer:
    """Calls cloud LLM to summarize conversations and extract facts."""

    def __init__(self, cloud_backend: LLMBackend):
        self.cloud = cloud_backend

    async def summarize(self, messages: list[dict]) -> str:
        conversation = self._format_conversation(messages)
        prompt = SUMMARIZE_PROMPT.format(conversation=conversation)
        try:
            result = await self.cloud.complete(
                messages=[{"role": "user", "content": prompt}]
            )
            return result.strip()
        except Exception as e:
            logger.error(f"[MEMORY] summarize failed: {e}")
            return ""

    async def extract_facts(
        self, messages: list[dict], existing_facts: list[Fact]
    ) -> list[Fact]:
        conversation = self._format_conversation(messages)
        existing_str = json.dumps(
            [{"content": f.content, "category": f.category} for f in existing_facts],
            ensure_ascii=False,
        ) if existing_facts else "[]"

        prompt = EXTRACT_FACTS_PROMPT.format(
            conversation=conversation, existing_facts=existing_str
        )
        try:
            result = await self.cloud.complete(
                messages=[{"role": "user", "content": prompt}]
            )
            items = json.loads(result.strip())
            if not isinstance(items, list):
                return []
            now = datetime.now(timezone.utc).isoformat()
            return [
                Fact(
                    content=item["content"],
                    category=item.get("category", "knowledge"),
                    created_at=now,
                    updated_at=now,
                    source_session="",
                )
                for item in items
                if isinstance(item, dict) and "content" in item
            ]
        except (json.JSONDecodeError, Exception) as e:
            logger.warning(f"[MEMORY] extract_facts failed: {e}")
            return []

    async def dedupe_facts(
        self, old_facts: list[Fact], new_facts: list[Fact], session_id: str
    ) -> list[Fact]:
        if not new_facts:
            return old_facts

        old_str = json.dumps(
            [{"content": f.content, "category": f.category} for f in old_facts],
            ensure_ascii=False,
        )
        new_str = json.dumps(
            [{"content": f.content, "category": f.category} for f in new_facts],
            ensure_ascii=False,
        )

        prompt = DEDUPE_FACTS_PROMPT.format(old_facts=old_str, new_facts=new_str)
        try:
            result = await self.cloud.complete(
                messages=[{"role": "user", "content": prompt}]
            )
            items = json.loads(result.strip())
            if not isinstance(items, list):
                return old_facts + new_facts
            now = datetime.now(timezone.utc).isoformat()
            return [
                Fact(
                    content=item["content"],
                    category=item.get("category", "knowledge"),
                    created_at=now,
                    updated_at=now,
                    source_session=session_id,
                )
                for item in items
                if isinstance(item, dict) and "content" in item
            ]
        except Exception as e:
            logger.warning(f"[MEMORY] dedupe_facts failed: {e}")
            return old_facts + new_facts

    def _format_conversation(self, messages: list[dict]) -> str:
        lines = []
        for msg in messages:
            role = "用户" if msg.get("role") == "user" else "助手"
            lines.append(f"{role}: {msg.get('content', '')}")
        return "\n".join(lines)
