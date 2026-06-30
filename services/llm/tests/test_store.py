import json
import pytest
from pathlib import Path
from memory.store import MemoryStore, SessionData, Fact


@pytest.fixture
def tmp_store(tmp_path):
    return MemoryStore(data_dir=tmp_path)


@pytest.mark.asyncio
async def test_save_and_load_session(tmp_store):
    data = SessionData(
        session_id="sess-001",
        messages=[
            {"role": "user", "content": "你好", "timestamp": "2026-05-29T10:00:00Z"},
            {"role": "assistant", "content": "你好呀", "timestamp": "2026-05-29T10:00:01Z"},
        ],
        summary="",
        last_summary_at="",
        created_at="2026-05-29T10:00:00Z",
    )
    await tmp_store.save_session("sess-001", data)
    loaded = await tmp_store.load_session("sess-001")
    assert loaded is not None
    assert loaded.session_id == "sess-001"
    assert len(loaded.messages) == 2
    assert loaded.messages[0]["content"] == "你好"


@pytest.mark.asyncio
async def test_load_nonexistent_session(tmp_store):
    assert await tmp_store.load_session("nonexistent") is None


@pytest.mark.asyncio
async def test_save_and_load_facts(tmp_store):
    facts = [
        Fact(content="用户名叫小明", category="user_info", created_at="2026-05-29T10:00:00Z", updated_at="2026-05-29T10:00:00Z", source_session="sess-001"),
        Fact(content="喜欢科幻电影", category="preference", created_at="2026-05-29T10:00:00Z", updated_at="2026-05-29T10:00:00Z", source_session="sess-001"),
    ]
    await tmp_store.save_facts(facts)
    loaded = await tmp_store.load_facts()
    assert len(loaded) == 2
    assert loaded[0].content == "用户名叫小明"
    assert loaded[1].category == "preference"


@pytest.mark.asyncio
async def test_load_empty_facts(tmp_store):
    assert await tmp_store.load_facts() == []


@pytest.mark.asyncio
async def test_load_knowledge(tmp_store):
    knowledge_file = tmp_store.data_dir / "knowledge.yaml"
    knowledge_file.write_text("主人叫张三，家里有一只猫叫咪咪。\n", encoding="utf-8")
    result = await tmp_store.load_knowledge()
    assert "张三" in result
    assert "咪咪" in result


@pytest.mark.asyncio
async def test_load_knowledge_missing(tmp_store):
    assert await tmp_store.load_knowledge() == ""


@pytest.mark.asyncio
async def test_list_sessions(tmp_store):
    for sid in ["sess-001", "sess-002"]:
        data = SessionData(session_id=sid, messages=[], summary="", last_summary_at="", created_at="")
        await tmp_store.save_session(sid, data)
    sessions = await tmp_store.list_sessions()
    assert set(sessions) == {"sess-001", "sess-002"}
