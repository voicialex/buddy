import pytest
from unittest.mock import AsyncMock
from pathlib import Path
from memory.store import MemoryStore, SessionData, Fact
from memory.summarizer import Summarizer
from memory.manager import MemoryManager


@pytest.fixture
def tmp_store(tmp_path):
    return MemoryStore(data_dir=tmp_path)


@pytest.fixture
def mock_summarizer():
    s = AsyncMock(spec=Summarizer)
    s.summarize.return_value = "用户之前聊了天气"
    s.extract_facts.return_value = []
    s.dedupe_facts.return_value = []
    return s


@pytest.fixture
def manager(tmp_store, mock_summarizer):
    config = {
        "summary_threshold": 4,
        "max_recent_turns": 3,
        "max_facts": 50,
        "session_timeout_sec": 300,
    }
    return MemoryManager(store=tmp_store, summarizer=mock_summarizer, config=config)


@pytest.mark.asyncio
async def test_build_context_empty_session(manager):
    messages = [{"role": "user", "content": "你好"}]
    result = await manager.build_context("sess-new", messages)
    assert result[-1]["content"] == "你好"


@pytest.mark.asyncio
async def test_build_context_with_knowledge(manager, tmp_store):
    (tmp_store.data_dir / "knowledge.yaml").write_text("主人叫小明。", encoding="utf-8")
    messages = [{"role": "user", "content": "你好"}]
    result = await manager.build_context("sess-new", messages)
    system_msgs = [m for m in result if m["role"] == "system"]
    assert any("小明" in m["content"] for m in system_msgs)


@pytest.mark.asyncio
async def test_build_context_with_facts(manager, tmp_store):
    facts = [Fact(content="用户喜欢猫", category="preference", created_at="", updated_at="", source_session="")]
    await tmp_store.save_facts(facts)
    messages = [{"role": "user", "content": "我想养宠物"}]
    result = await manager.build_context("sess-new", messages)
    system_msgs = [m for m in result if m["role"] == "system"]
    assert any("猫" in m["content"] for m in system_msgs)


@pytest.mark.asyncio
async def test_save_turn(manager):
    await manager.save_turn("sess-001", "你好", "你好呀")
    session = await manager.store.load_session("sess-001")
    assert session is not None
    assert len(session.messages) == 2
    assert session.messages[0]["role"] == "user"
    assert session.messages[1]["role"] == "assistant"


@pytest.mark.asyncio
async def test_save_turn_triggers_summarize(manager, mock_summarizer):
    # summary_threshold=4, max_recent_turns=3 → trigger when len(msgs) >= 4*2 + 6 = 14
    # need 7 turns (14 messages) to trigger compression
    for i in range(7):
        await manager.save_turn("sess-001", f"问题{i}", f"回答{i}")
    assert mock_summarizer.summarize.called


@pytest.mark.asyncio
async def test_on_session_end(manager, mock_summarizer):
    await manager.save_turn("sess-001", "你好", "你好呀")
    await manager.on_session_end("sess-001")
    mock_summarizer.summarize.assert_called()
    mock_summarizer.extract_facts.assert_called()
