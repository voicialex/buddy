"""Summarizer — calls cloud LLM for summarization and fact extraction."""
import json
import logging
import re
from datetime import datetime, timezone

from backends.base import LLMBackend
from .store import Fact
from .prompts import SUMMARIZE_PROMPT, EXTRACT_FACTS_PROMPT, DEDUPE_FACTS_PROMPT

logger = logging.getLogger("llm_server")

# LLM often wraps JSON in ```json ... ``` fences
_JSON_BLOCK_RE = re.compile(r"```(?:json)?\s*(\[.*?\])\s*```", re.DOTALL)


def _extract_json_array(text: str) -> list:
    """Try to extract a JSON array from LLM output, tolerating markdown fences."""
    m = _JSON_BLOCK_RE.search(text)
    return json.loads(m.group(1) if m else text.strip())


class Summarizer:
    """Calls cloud LLM to summarize conversations and extract facts.

    Falls back to local_backend when cloud is unavailable (local-only devices).
    """

    def __init__(self, cloud_backend: LLMBackend | None, local_backend: LLMBackend | None = None):
        self.cloud = cloud_backend
        self.local = local_backend

    def _pick_backend(self) -> LLMBackend | None:
        """Prefer cloud; fall back to local for local-only deployments."""
        return self.cloud or self.local

    async def summarize(self, messages: list[dict]) -> str:
        backend = self._pick_backend()
        if backend is None:
            logger.warning("[MEMORY] summarize skipped: no backend available")
            return ""
        conversation = self._format_conversation(messages)
        prompt = SUMMARIZE_PROMPT.format(conversation=conversation)
        try:
            result = await backend.complete(
                messages=[{"role": "user", "content": prompt}]
            )
            return result.strip()
        except Exception as e:
            logger.error(f"[MEMORY] summarize failed: {e}")
            return ""

    async def extract_facts(
        self, messages: list[dict], existing_facts: list[Fact]
    ) -> list[Fact]:
        backend = self._pick_backend()
        if backend is None:
            logger.warning("[MEMORY] extract_facts skipped: no backend available")
            return []
        conversation = self._format_conversation(messages)
        existing_str = json.dumps(
            [{"content": f.content, "category": f.category} for f in existing_facts],
            ensure_ascii=False,
        ) if existing_facts else "[]"

        prompt = EXTRACT_FACTS_PROMPT.format(
            conversation=conversation, existing_facts=existing_str
        )
        try:
            result = await backend.complete(
                messages=[{"role": "user", "content": prompt}]
            )
            items = _extract_json_array(result.strip())
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
        except json.JSONDecodeError as e:
            logger.warning(f"[MEMORY] extract_facts JSON parse failed: {e}")
            return []
        except Exception as e:
            logger.error(f"[MEMORY] extract_facts failed: {e}")
            return []

    async def dedupe_facts(
        self, old_facts: list[Fact], new_facts: list[Fact], session_id: str
    ) -> list[Fact]:
        if not new_facts:
            return old_facts
        backend = self._pick_backend()
        if backend is None:
            logger.warning("[MEMORY] dedupe_facts skipped: no backend available")
            return old_facts + new_facts

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
            result = await backend.complete(
                messages=[{"role": "user", "content": prompt}]
            )
            items = _extract_json_array(result.strip())
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
        except json.JSONDecodeError as e:
            logger.warning(f"[MEMORY] dedupe_facts JSON parse failed: {e}")
            return old_facts + new_facts
        except Exception as e:
            logger.warning(f"[MEMORY] dedupe_facts failed: {e}")
            return old_facts + new_facts

    def _format_conversation(self, messages: list[dict]) -> str:
        lines = []
        for msg in messages:
            role = "用户" if msg.get("role") == "user" else "助手"
            lines.append(f"{role}: {msg.get('content', '')}")
        return "\n".join(lines)
