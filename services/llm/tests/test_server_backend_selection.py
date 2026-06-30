from backends.rk_llm import RkLlmBackend
from backends.vllm import VLLMBackend
from server import create_backend, resolve_active_backend


def test_create_backend_rk_llm():
    backend = create_backend(
        {
            "backend": "rk_llm",
            "base_url": "http://127.0.0.1:18080",
            "model": "buddy-rk",
        }
    )
    assert isinstance(backend, RkLlmBackend)
    assert backend.model == "buddy-rk"


def test_resolve_active_backend_prefers_env_override(monkeypatch):
    monkeypatch.setenv("BUDDY_LLM_BACKEND", "vllm")
    local_cfg = {"active_backend": "ollama", "backends": {"ollama": {}, "rk_llm": {}, "vllm": {}}}
    assert resolve_active_backend(local_cfg) == "vllm"


def test_resolve_active_backend_explicit_config():
    local_cfg = {"active_backend": "rk_llm", "backends": {"ollama": {}, "rk_llm": {}}}
    assert resolve_active_backend(local_cfg) == "rk_llm"


def test_resolve_active_backend_defaults_to_ollama():
    local_cfg = {"backends": {"ollama": {}}}
    assert resolve_active_backend(local_cfg) == "ollama"
