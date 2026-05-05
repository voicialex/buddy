# Text-Based Testing Guide

No audio hardware needed. Manually publish ROS 2 topics to simulate voice input.

## Modules & Communication

Three modules are involved in local LLM testing:

```
Audio ──topic──> Brain ──topic──> LocalLLM ──HTTP──> Ollama
  ^                │                  │
  │                │                  │ (streaming chunks)
  └──topic─────────┘<──topic─────────┘
```

| Module | Role | Type |
|--------|------|------|
| Audio | Wake word detection, ASR input, TTS playback | Disabled, simulated by manual publish |
| Brain | State machine, sentence segmentation, orchestrates flow | ROS 2 lifecycle node |
| LocalLLM | Bridges ROS topics to Ollama HTTP API | ROS 2 lifecycle node |
| Ollama | Local LLM inference (gemma4:e2b) | External HTTP service |

Topic flow (one conversation cycle):

```
1. /audio/wake_word    (String)       → Brain: IDLE → LISTENING
2. /audio/asr_text     (String)       → Brain: LISTENING → REQUESTING, publishes InferenceRequest
3. /brain/request      (InferenceRequest) → LocalLLM: calls Ollama HTTP API
4. localhost:11434/api/chat            → Ollama: streaming NDJSON response
5. /inference/local_chunk (InferenceChunk) → Brain: segments text into sentences
6. /brain/sentence     (Sentence)     → (would go to TTS for playback)
7. /brain/response     (String)       ← Brain: complete response (CLI friendly)
8. /audio/tts_done     (Empty)        → Brain: SPEAKING → IDLE
```

## Prerequisites

```bash
# 1. Ollama running
OLLAMA_MODELS=/home/seb/buddy_ws/ollama/models ollama serve

# 2. ros2 CLI (Jazzy)
source /opt/ros/jazzy/setup.bash
```

## Start Buddy

```bash
./run.sh
```

Confirm you see `All nodes active. Spinning...` in the output.

## One Conversation Cycle

Each round needs 3 steps: **wake → speak → done**.

```bash
source /opt/ros/jazzy/setup.bash

# Step 1: Wake (trigger LISTENING state)
ros2 topic pub --once /audio/wake_word std_msgs/msg/String '{data: "wake"}'

# Step 2: Speak (send text, triggers inference)
ros2 topic pub --once /audio/asr_text std_msgs/msg/String '{data: "你好"}'

# Step 3: Reset (tell brain TTS finished, back to IDLE)
# Wait for LLM response first, then:
ros2 topic pub --once /audio/tts_done std_msgs/msg/Empty '{}'
```

Without step 3 the state machine is stuck at SPEAKING and ignores all new input.

## Monitor Output

```bash
source /opt/ros/jazzy/setup.bash

# Final response (std_msgs/String, works with Jazzy CLI)
ros2 topic echo /brain/response
```

This outputs the complete response text when inference finishes, e.g.:

```
data: '你好！有什么可以帮你的吗？😊'
```

You can also watch buddy's log for sentence-by-sentence output:

```
[INFO] [brain]: Local sentence #0: 你好！
[INFO] [brain]: Local sentence #1: 有什么可以帮你的吗？
[INFO] [brain]: Local response: 你好！有什么可以帮你的吗？😊
```

## Module Config

`src/buddy_app/params/modules.yaml`:

```yaml
modules:
  audio: false     # no microphone/speaker
  vision: false    # no camera needed
  brain: true      # state machine
  cloud: false     # test local LLM only
  local_llm: true  # Ollama
```

Enable `cloud: true` to test the dual-brain flow (cloud replaces local response).

## State Machine

```
IDLE → (wake_word) → LISTENING → (asr_text) → REQUESTING → (cloud/local done) → SPEAKING → (tts_done) → IDLE
```

## Common Issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| No response after step 2 | Ollama not running | `ollama serve` |
| "Previous request still in progress" | Rapid fire requests | Wait longer between tests |
| State stuck, no new responses | Missing step 3 | Send `tts_done` |
| "sequence size exceeds remaining buffer" | Jazzy/Humble DDS mismatch | Harmless, ignore |
