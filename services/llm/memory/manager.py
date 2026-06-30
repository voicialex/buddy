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
        self._session_locks: dict[str, asyncio.Lock] = {}

    def _get_session_lock(self, session_id: str) -> asyncio.Lock:
        return self._session_locks.setdefault(session_id, asyncio.Lock())

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
        knowledge = await self.store.load_knowledge()
        facts = await self.store.load_facts()
        session = await self.store.load_session(session_id)

        memory_parts = []
        if knowledge:
            memory_parts.append(f"[记忆-静态知识]\n{knowledge}")
        if facts:
            facts_str = "\n".join(f"- {f.content}" for f in facts[:self.max_facts])
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
        lock = self._get_session_lock(session_id)

        async with lock:
            session = await self.store.load_session(session_id)
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
            await self.store.save_session(session_id, session)

            # 触发压缩的不变量：消息数 >= 触发阈值 + 保留窗口的一半
            # 避免压缩后立即再次触发（旧逻辑 summary_threshold=8 + keep_count=12 会每 2 轮压一次）
            keep_count = self.max_recent_turns * 2
            if len(session.messages) >= self.summary_threshold * 2 + keep_count:
                await self._compress_session(session_id)

    async def on_session_end(self, session_id: str):
        """Called when session ends. Does final summarization + fact extraction."""
        lock = self._get_session_lock(session_id)
        async with lock:
            session = await self.store.load_session(session_id)
            if not session or not session.messages:
                self._session_locks.pop(session_id, None)
                return

            logger.info(f"[MEMORY] session_end: {session_id}, {len(session.messages)} messages")

            summary = await self.summarizer.summarize(session.messages)
            if summary:
                session.summary = summary
                session.last_summary_at = datetime.now(timezone.utc).isoformat()
                await self.store.save_session(session_id, session)

            existing_facts = await self.store.load_facts()
            new_facts = await self.summarizer.extract_facts(session.messages, existing_facts)
            if new_facts:
                merged = await self.summarizer.dedupe_facts(existing_facts, new_facts, session_id)
                # Sort by updated_at before truncating so recent facts are kept
                merged.sort(key=lambda f: f.updated_at, reverse=True)
                merged = merged[:self.max_facts]
                await self.store.save_facts(merged)
                logger.info(f"[MEMORY] extracted {len(new_facts)} facts, total: {len(merged)}")

            self._session_locks.pop(session_id, None)

    async def _compress_session(self, session_id: str):
        """Compress old messages into summary, keep only recent turns."""
        try:
            session = await self.store.load_session(session_id)
            if not session:
                return

            summary = await self.summarizer.summarize(session.messages)
            if not summary:
                return

            keep_count = self.max_recent_turns * 2
            session.summary = summary
            session.messages = session.messages[-keep_count:]
            session.last_summary_at = datetime.now(timezone.utc).isoformat()
            await self.store.save_session(session_id, session)
            logger.info(f"[MEMORY] compressed session {session_id}, kept {keep_count} messages, summary_len={len(summary)}")
        except Exception as e:
            logger.error(f"[MEMORY] compress failed for {session_id}: {e}")
