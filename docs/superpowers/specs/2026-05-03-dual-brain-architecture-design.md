# Dual-Brain Architecture Design (Edge-Cloud Collaborative Inference)

Date: 2026-05-03
Status: Approved

## Background

Buddy currently uses a single cloud model (Doubao) for all inference. We adopt an edge-cloud collaborative approach ("dual-brain" / "大小脑") to reduce perceived latency: a local Gemma 4 E2B model provides fast initial responses while the cloud model generates higher-quality answers.

## Decision: Parallel-Augmented with Replacement

- Local model streams a quick response, TTS plays it immediately
- When cloud response arrives, TTS is interrupted and replaced with the cloud response
- Both models run in parallel; the user hears something fast, then gets the real answer

## Architecture

### New Package: `buddy_local_llm`

Independent ROS 2 component, parallel to `buddy_cloud`.

```
                     ┌──────────────┐
                     │  buddy_brain │
                     └──────┬───────┘
                  publishes  │        subscribes
            /brain/request   │   ┌─────────────────────┐
                     ┌───────┤   │ /inference/local_chunk│
                     │       │   │ /inference/cloud_chunk│
               ┌─────▼──┐ ┌──▼─────┐
               │local_llm│ │ cloud  │
               └────────┘ └────────┘
```

### Message Design

Topic identity = source identity. No `source` field needed.

| Message | Purpose | Fields |
|---------|---------|--------|
| `InferenceRequest` | brain → models | Same as old `CloudRequest`: trigger_type, user_text, emotion, dialog_history, system_prompt, image |
| `InferenceChunk` | models → brain | Same as old `CloudChunk`: session_id, chunk_text, is_final |

Topics:

| Topic | Publisher | Subscriber | Message |
|-------|-----------|------------|---------|
| `/brain/request` | buddy_brain | buddy_local_llm, buddy_cloud | InferenceRequest |
| `/inference/local_chunk` | buddy_local_llm | buddy_brain | InferenceChunk |
| `/inference/cloud_chunk` | buddy_cloud | buddy_brain | InferenceChunk |

### buddy_local_llm Internals

**ollama_client.cpp**: libcurl HTTP client for ollama streaming API.

```
POST http://localhost:11434/api/chat
{model: "gemma4:e2b", messages: [...], stream: true}
```

- Parse SSE lines, extract `message.content` per chunk
- 5-second timeout; on failure publish empty chunk with is_final=true

**local_llm_node.cpp**:

- Subscribe `/brain/request` → call ollama streaming → publish `/inference/local_chunk`
- No image processing (local model is text-only)
- No dialog history (local only generates quick replies)
- Fixed short system prompt for natural quick responses

**Config** (`params/local_llm.yaml`):

```yaml
buddy_local_llm:
  ros__parameters:
    model_name: "gemma4:e2b"
    api_url: "http://localhost:11434"
    timeout_seconds: 5
    system_prompt: "你是一个友好的机器人助手，请用简短自然的方式回复"
```

### buddy_brain Changes

State machine: 5 states unchanged (IDLE, LISTENING, EMOTION_TRIGGER, REQUESTING, SPEAKING).

Subscribe changes:

```
Remove: /cloud/response (CloudChunk)
Add:    /inference/local_chunk (InferenceChunk)
Add:    /inference/cloud_chunk (InferenceChunk)
```

Publish changes:

```
/brain/cloud_request → /brain/request
CloudRequest → InferenceRequest
```

REQUESTING state replacement logic:

```cpp
local_chunk_callback(chunk):
    if state_ != REQUESTING: return
    feed_to_sentence(chunk)

cloud_chunk_callback(chunk):
    if state_ != REQUESTING: return
    if first_cloud_chunk_:
        tts_interrupt()
        sentence_clear()
        first_cloud_chunk_ = false
    feed_to_sentence(chunk)
    if chunk.is_final:
        state_ = SPEAKING
```

Member `first_cloud_chunk_` (bool) resets to `true` on entering REQUESTING.

### buddy_cloud Changes

Minimal: rename topic and message types only.

```
Subscribe: /brain/cloud_request → /brain/request (InferenceRequest)
Publish:   /cloud/response → /inference/cloud_chunk (InferenceChunk)
```

Internal HTTP logic, streaming parser, libcurl code unchanged.

### buddy_app Changes

Add `buddy_local_llm` to component loading list:

```
Loading order:
  buddy_audio → buddy_vision → buddy_cloud → buddy_local_llm → buddy_brain
```

### Package Structure: buddy_local_llm

```
src/buddy_local_llm/
├── CMakeLists.txt
├── package.xml
├── include/buddy_local_llm/
│   └── local_llm_node.hpp
├── src/
│   ├── local_llm_node.cpp
│   └── ollama_client.cpp
└── test/
    └── test_local_llm_node.cpp
```

### Build Order

```
buddy_interfaces → buddy_cloud → buddy_local_llm → buddy_brain → buddy_app
                                       ↑ new
```

### Interfaces Changes

Rename (field contents unchanged):

- `CloudRequest` → `InferenceRequest`
- `CloudChunk` → `InferenceChunk`

### Docs to Update

- `docs/architecture.md` — add buddy_local_llm, update topology diagram
- `docs/communication_protocol.md` — update topic names and message types
- `docs/plan.md` — update roadmap

### Unchanged Packages

- buddy_audio — zero changes
- buddy_vision — zero changes
- State machine states — unchanged
- Existing TTS and sentence segmentation logic — unchanged
