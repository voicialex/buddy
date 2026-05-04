---
type: "query"
date: "2026-05-04T12:02:56.307208+00:00"
question: "How does the Dual-Brain architecture route inference between local LLM and cloud"
contributor: "graphify"
source_nodes: ["Dual-Brain Architecture (Phase 4)", "/inference/local_chunk Topic", "/inference/cloud_chunk Topic", "brain_component Library"]
---

# Q: How does the Dual-Brain architecture route inference between local LLM and cloud

## Answer

BrainNode is the central router. It receives /brain/request and fans out to both CloudClientNode (Doubao API) and LocalLlmNode (Ollama) simultaneously. Results flow back via /inference/cloud_chunk and /inference/local_chunk topics. BrainNode merges these streams and produces /brain/sentence for TTS. The dual-brain timing principle (communication_protocol.md:51) governs how BrainNode handles race conditions between local and cloud responses.

## Source Nodes

- Dual-Brain Architecture (Phase 4)
- /inference/local_chunk Topic
- /inference/cloud_chunk Topic
- brain_component Library