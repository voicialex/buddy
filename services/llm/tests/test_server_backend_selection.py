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
    local_cfg = {"active_backend": "auto", "backends": {"ollama": {}, "rk_llm": {}, "vllm": {}}}
    assert resolve_active_backend(local_cfg) == "vllm"


def test_resolve_active_backend_auto_selects_rk_llm_for_arm64_npu(monkeypatch):
    monkeypatch.delenv("BUDDY_LLM_BACKEND", raising=False)
    monkeypatch.setenv("BUDDY_TARGET_ARCH", "arm64")
    monkeypatch.setenv("BUDDY_TARGET_DEVICE", "npu")
    local_cfg = {"active_backend": "auto", "backends": {"ollama": {}, "rk_llm": {}}}
    assert resolve_active_backend(local_cfg) == "rk_llm"


def test_resolve_active_backend_auto_falls_back_to_ollama(monkeypatch):
    monkeypatch.delenv("BUDDY_LLM_BACKEND", raising=False)
    monkeypatch.setenv("BUDDY_TARGET_ARCH", "x86_64")
    monkeypatch.setenv("BUDDY_TARGET_DEVICE", "cpu")
    local_cfg = {"active_backend": "auto", "backends": {"ollama": {}, "rk_llm": {}}}
    assert resolve_active_backend(local_cfg) == "ollama"


def test_resolve_active_backend_non_auto_passthrough(monkeypatch):
    monkeypatch.delenv("BUDDY_LLM_BACKEND", raising=False)
    local_cfg = {"active_backend": "vllm", "backends": {"vllm": {}, "ollama": {}}}
    assert resolve_active_backend(local_cfg) == "vllm"
    backend = create_backend({"backend": "vllm"})
    assert isinstance(backend, VLLMBackend)
