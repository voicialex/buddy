import json
from dataclasses import dataclass, asdict
from pathlib import Path


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


class MemoryStore:
    """Pure I/O layer for memory persistence. No LLM logic."""

    def __init__(self, data_dir: Path):
        self.data_dir = Path(data_dir)
        self.sessions_dir = self.data_dir / "sessions"
        self.sessions_dir.mkdir(parents=True, exist_ok=True)

    def load_session(self, session_id: str) -> SessionData | None:
        path = self.sessions_dir / f"{session_id}.json"
        if not path.exists():
            return None
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return SessionData(**data)

    def save_session(self, session_id: str, data: SessionData):
        path = self.sessions_dir / f"{session_id}.json"
        with open(path, "w", encoding="utf-8") as f:
            json.dump(asdict(data), f, ensure_ascii=False, indent=2)

    def list_sessions(self) -> list[str]:
        return [p.stem for p in self.sessions_dir.glob("*.json")]

    def load_facts(self) -> list[Fact]:
        path = self.data_dir / "facts.json"
        if not path.exists():
            return []
        with open(path, "r", encoding="utf-8") as f:
            items = json.load(f)
        return [Fact(**item) for item in items]

    def save_facts(self, facts: list[Fact]):
        path = self.data_dir / "facts.json"
        with open(path, "w", encoding="utf-8") as f:
            json.dump([asdict(f) for f in facts], f, ensure_ascii=False, indent=2)

    def load_knowledge(self) -> str:
        path = self.data_dir / "knowledge.yaml"
        if not path.exists():
            return ""
        return path.read_text(encoding="utf-8").strip()
