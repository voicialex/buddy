"""MemoryManager — core orchestrator for the memory system."""
import asyncio
import logging
from datetime import datetime, timezone
from pathlib import Path

from .store import MemoryStore, SessionData, Fact
from .summarizer import Summarizer

logger = logging.getLogger("llm_server")


class MemoryManager:
    """Single entry point for memory operations. Router/Server interact only with this."""

    def __init__(self, store: MemoryStore, summarizer: Summarizer, config: dict):
        self.store = store
        self.summarizer = summarizer
        self.summary_threshold = config.get("summary_threshold", 8)
        self.max_recent_turns = config.get("max_recent_turns", 6)
        self.max_facts = config.get("max_facts", 50)
        self.session_timeout_sec = config.get("session_timeout_sec", 300)
        self._last_activity: dict[str, datetime] = {}

    async def build_context(self, session_id: str, messages: list[dict]) -> list[dict]:
        """
        Build enhanced messages list with memory context injected.

        Output structure:
        [
            {role: "system", content: knowledge + facts + previous_summary},
            ...recent turns from current session...,
            ...incoming messages...
        ]
        """
        knowledge = self.store.load_knowledge()
        facts = self.store.load_facts()
        session = self.store.load_session(session_id)

        memory_parts = []
        if knowledge:
            memory_parts.append(f"[记忆-静态知识]\n{knowledge}")
        if facts:
            relevant = self.get_relevant_facts(
                messages[-1].get("content", "") if messages else ""
            )
            if relevant:
                facts_str = "\n".join(f"- {f.content}" for f in relevant)
                memory_parts.append(f"[记忆-用户信息]\n{facts_str}")
        if session and session.summary:
            memory_parts.append(f"[记忆-上次对话摘要]\n{session.summary}")

        result = []
        if memory_parts:
            result.append({"role": "system", "content": "\n\n".join(memory_parts)})

        if session and session.messages:
            recent = session.messages[-(self.max_recent_turns * 2):]
            for msg in recent:
                result.append({"role": msg["role"], "content": msg["content"]})

        result.extend(messages)
        return result

    async def save_turn(self, session_id: str, user_msg: str, assistant_msg: str):
        """Save a conversation turn. Triggers summarization if threshold exceeded."""
        now = datetime.now(timezone.utc).isoformat()
        self._last_activity[session_id] = datetime.now(timezone.utc)

        session = self.store.load_session(session_id)
        if session is None:
            session = SessionData(
                session_id=session_id,
                messages=[],
                summary="",
                last_summary_at="",
                created_at=now,
            )

        session.messages.append({"role": "user", "content": user_msg, "timestamp": now})
        session.messages.append({"role": "assistant", "content": assistant_msg, "timestamp": now})
        self.store.save_session(session_id, session)

        turn_count = len(session.messages) // 2
        if turn_count >= self.summary_threshold:
            await self._compress_session(session_id)
            # 说明：测试需要同步 await，实际部署可换回异步任务

    async def on_session_end(self, session_id: str):
        """Called when session ends. Does final summarization + fact extraction."""
        session = self.store.load_session(session_id)
        if not session or not session.messages:
            return

        logger.info(f"[MEMORY] session_end: {session_id}, {len(session.messages)} messages")

        summary = await self.summarizer.summarize(session.messages)
        if summary:
            session.summary = summary
            session.last_summary_at = datetime.now(timezone.utc).isoformat()
            self.store.save_session(session_id, session)

        existing_facts = self.store.load_facts()
        new_facts = await self.summarizer.extract_facts(session.messages, existing_facts)
        if new_facts:
            merged = await self.summarizer.dedupe_facts(existing_facts, new_facts, session_id)
            if len(merged) > self.max_facts:
                merged = merged[-self.max_facts:]
            self.store.save_facts(merged)
            logger.info(f"[MEMORY] extracted {len(new_facts)} facts, total: {len(merged)}")

    def get_relevant_facts(self, query: str) -> list[Fact]:
        """Simple keyword-based retrieval of relevant facts."""
        if not query:
            return self.store.load_facts()

        all_facts = self.store.load_facts()
        if not all_facts:
            return []

        scored = []
        query_chars = set(query)
        for fact in all_facts:
            overlap = len(query_chars & set(fact.content))
            if overlap > 0:
                scored.append((overlap, fact))

        if not scored:
            return all_facts

        scored.sort(key=lambda x: x[0], reverse=True)
        return [f for _, f in scored[:10]]

    async def _compress_session(self, session_id: str):
        """Compress old messages into summary, keep only recent turns."""
        try:
            session = self.store.load_session(session_id)
            if not session:
                return

            summary = await self.summarizer.summarize(session.messages)
            if not summary:
                return

            keep_count = self.max_recent_turns * 2
            session.summary = summary
            session.messages = session.messages[-keep_count:]
            session.last_summary_at = datetime.now(timezone.utc).isoformat()
            self.store.save_session(session_id, session)
            logger.info(f"[MEMORY] compressed session {session_id}, kept {keep_count} messages")
        except Exception as e:
            logger.error(f"[MEMORY] compress failed for {session_id}: {e}")
