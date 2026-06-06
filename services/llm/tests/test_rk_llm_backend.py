from backends.rk_llm import RkLlmBackend


def test_default_endpoint_rkllm_demo():
    backend = RkLlmBackend()
    assert backend.base_url == "http://127.0.0.1:8080"
    assert backend.endpoint == "/rkllm_chat"


def test_default_endpoint_openai_style():
    backend = RkLlmBackend(base_url="http://127.0.0.1:18080", api_style="openai")
    assert backend.endpoint == "/v1/chat/completions"


def test_custom_endpoint_normalization():
    backend = RkLlmBackend(endpoint="rkllm_chat")
    assert backend.endpoint == "/rkllm_chat"


def test_extract_stream_text_from_openai_chunk():
    data = {"choices": [{"delta": {"content": "A"}}]}
    assert RkLlmBackend._extract_stream_text(data) == "A"


def test_extract_stream_text_from_rkllm_chunk():
    data = {"choices": [{"message": {"role": "assistant", "content": "B"}}]}
    assert RkLlmBackend._extract_stream_text(data) == "B"


def test_extract_complete_text():
    data = {"choices": [{"message": {"role": "assistant", "content": "Hello"}}]}
    assert RkLlmBackend._extract_complete_text(data) == "Hello"
