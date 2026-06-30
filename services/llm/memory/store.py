import asyncio
import json
import logging
import re
from dataclasses import dataclass, asdict
from pathlib import Path

logger = logging.getLogger("llm_server")

_SESSION_ID_RE = re.compile(r"[A-Za-z0-9_\-]{1,64}")


@dataclass
class Fact:
    content: str
    category: str  # "user_info" | "preference" | "knowledge"
    created_at: str
    updated_at: str
    source_session: str


@dataclass
class SessionData:
    session_id: str
    messages: list[dict]  # [{role, content, timestamp}]
    summary: str
    last_summary_at: str
    created_at: str


def _validate_session_id(session_id: str) -> None:
    if not _SESSION_ID_RE.fullmatch(session_id):
        raise ValueError(f"Invalid session_id: {session_id!r}")


class MemoryStore:
    """Pure I/O layer for memory persistence. No LLM logic."""

    def __init__(self, data_dir: Path):
        self.data_dir = Path(data_dir)
        self.sessions_dir = self.data_dir / "sessions"
        self.sessions_dir.mkdir(parents=True, exist_ok=True)

    async def load_session(self, session_id: str) -> SessionData | None:
        _validate_session_id(session_id)
        path = self.sessions_dir / f"{session_id}.json"
        if not path.exists():
            return None
        try:
            data = json.loads(
                await asyncio.to_thread(path.read_text, encoding="utf-8")
            )
        except (json.JSONDecodeError, TypeError) as e:
            logger.warning("Failed to load session %s: %s", session_id, e)
            return None
        try:
            return SessionData(**data)
        except TypeError as e:
            logger.warning("Invalid session data for %s: %s", session_id, e)
            return None

    async def save_session(self, session_id: str, data: SessionData):
        _validate_session_id(session_id)
        path = self.sessions_dir / f"{session_id}.json"
        content = json.dumps(asdict(data), ensure_ascii=False, indent=2)
        await asyncio.to_thread(path.write_text, content, encoding="utf-8")

    async def list_sessions(self) -> list[str]:
        def _list():
            return [p.stem for p in self.sessions_dir.glob("*.json")]
        return await asyncio.to_thread(_list)

    async def load_facts(self) -> list[Fact]:
        path = self.data_dir / "facts.json"
        if not path.exists():
            return []
        try:
            items = json.loads(
                await asyncio.to_thread(path.read_text, encoding="utf-8")
            )
        except (json.JSONDecodeError, TypeError) as e:
            logger.warning("Failed to load facts: %s", e)
            return []
        try:
            return [Fact(**item) for item in items]
        except TypeError as e:
            logger.warning("Invalid facts data: %s", e)
            return []

    async def save_facts(self, facts: list[Fact]):
        path = self.data_dir / "facts.json"
        content = json.dumps([asdict(f) for f in facts], ensure_ascii=False, indent=2)
        await asyncio.to_thread(path.write_text, content, encoding="utf-8")

    async def load_knowledge(self) -> str:
        path = self.data_dir / "knowledge.yaml"
        if not path.exists():
            return ""
        try:
            return (await asyncio.to_thread(path.read_text, encoding="utf-8")).strip()
        except (OSError, UnicodeDecodeError) as e:
            logger.warning("Failed to load knowledge: %s", e)
            return ""
