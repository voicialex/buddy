import pytest
from unittest.mock import AsyncMock
from memory.store import Fact
from memory.summarizer import Summarizer


@pytest.fixture
def mock_backend():
    return AsyncMock()


@pytest.fixture
def summarizer(mock_backend):
    return Summarizer(cloud_backend=mock_backend)


@pytest.mark.asyncio
async def test_summarize(summarizer, mock_backend):
    mock_backend.complete.return_value = "用户问了天气，助手建议查看天气预报。情绪正常。"
    messages = [
        {"role": "user", "content": "今天天气怎么样"},
        {"role": "assistant", "content": "建议你查看天气预报哦"},
    ]
    result = await summarizer.summarize(messages)
    assert "天气" in result
    mock_backend.complete.assert_called_once()


@pytest.mark.asyncio
async def test_extract_facts_returns_list(summarizer, mock_backend):
    mock_backend.complete.return_value = '[{"content": "用户叫小明", "category": "user_info"}]'
    messages = [
        {"role": "user", "content": "我叫小明"},
        {"role": "assistant", "content": "你好小明"},
    ]
    facts = await summarizer.extract_facts(messages, existing_facts=[])
    assert len(facts) == 1
    assert facts[0].content == "用户叫小明"
    assert facts[0].category == "user_info"


@pytest.mark.asyncio
async def test_extract_facts_empty(summarizer, mock_backend):
    mock_backend.complete.return_value = "[]"
    messages = [
        {"role": "user", "content": "你好"},
        {"role": "assistant", "content": "你好呀"},
    ]
    facts = await summarizer.extract_facts(messages, existing_facts=[])
    assert facts == []


@pytest.mark.asyncio
async def test_extract_facts_invalid_json(summarizer, mock_backend):
    mock_backend.complete.return_value = "这不是JSON"
    messages = [{"role": "user", "content": "你好"}]
    facts = await summarizer.extract_facts(messages, existing_facts=[])
    assert facts == []


@pytest.mark.asyncio
async def test_dedupe_facts(summarizer, mock_backend):
    mock_backend.complete.return_value = '[{"content": "用户叫小华", "category": "user_info"}, {"content": "喜欢科幻", "category": "preference"}]'
    old_facts = [Fact(content="用户叫小明", category="user_info", created_at="", updated_at="", source_session="")]
    new_facts = [Fact(content="用户叫小华", category="user_info", created_at="", updated_at="", source_session="")]
    result = await summarizer.dedupe_facts(old_facts, new_facts, session_id="sess-002")
    assert len(result) == 2
    assert result[0].content == "用户叫小华"
