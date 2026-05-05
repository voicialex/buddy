# Graph Report - buddy_robot  (2026-05-05)

## Corpus Check
- 27 files · ~11,137 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 238 nodes · 257 edges · 44 communities detected
- Extraction: 88% EXTRACTED · 12% INFERRED · 0% AMBIGUOUS · INFERRED: 32 edges (avg confidence: 0.9)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 2|Community 2]]
- [[_COMMUNITY_Community 3|Community 3]]
- [[_COMMUNITY_Community 4|Community 4]]
- [[_COMMUNITY_Community 5|Community 5]]
- [[_COMMUNITY_Community 6|Community 6]]
- [[_COMMUNITY_Community 7|Community 7]]
- [[_COMMUNITY_Community 8|Community 8]]
- [[_COMMUNITY_Community 9|Community 9]]
- [[_COMMUNITY_Community 10|Community 10]]
- [[_COMMUNITY_Community 11|Community 11]]
- [[_COMMUNITY_Community 12|Community 12]]
- [[_COMMUNITY_Community 13|Community 13]]
- [[_COMMUNITY_Community 14|Community 14]]
- [[_COMMUNITY_Community 15|Community 15]]
- [[_COMMUNITY_Community 16|Community 16]]
- [[_COMMUNITY_Community 17|Community 17]]
- [[_COMMUNITY_Community 18|Community 18]]
- [[_COMMUNITY_Community 19|Community 19]]
- [[_COMMUNITY_Community 20|Community 20]]
- [[_COMMUNITY_Community 21|Community 21]]
- [[_COMMUNITY_Community 22|Community 22]]
- [[_COMMUNITY_Community 26|Community 26]]
- [[_COMMUNITY_Community 27|Community 27]]
- [[_COMMUNITY_Community 28|Community 28]]
- [[_COMMUNITY_Community 29|Community 29]]
- [[_COMMUNITY_Community 30|Community 30]]
- [[_COMMUNITY_Community 31|Community 31]]
- [[_COMMUNITY_Community 32|Community 32]]
- [[_COMMUNITY_Community 33|Community 33]]
- [[_COMMUNITY_Community 34|Community 34]]
- [[_COMMUNITY_Community 35|Community 35]]
- [[_COMMUNITY_Community 36|Community 36]]
- [[_COMMUNITY_Community 37|Community 37]]
- [[_COMMUNITY_Community 38|Community 38]]
- [[_COMMUNITY_Community 39|Community 39]]
- [[_COMMUNITY_Community 40|Community 40]]
- [[_COMMUNITY_Community 41|Community 41]]
- [[_COMMUNITY_Community 42|Community 42]]
- [[_COMMUNITY_Community 43|Community 43]]
- [[_COMMUNITY_Community 44|Community 44]]
- [[_COMMUNITY_Community 45|Community 45]]
- [[_COMMUNITY_Community 46|Community 46]]

## God Nodes (most connected - your core abstractions)
1. `brain_component Library` - 18 edges
2. `vision_component Library` - 12 edges
3. `cloud_component Library` - 11 edges
4. `local_llm_component Library` - 10 edges
5. `audio_component Library` - 9 edges
6. `transition()` - 8 edges
7. `buddy_main Executable` - 8 edges
8. `on_cloud_chunk()` - 6 edges
9. `process_chunk()` - 5 edges
10. `on_local_chunk()` - 5 edges

## Surprising Connections (you probably didn't know these)
- `ament_cmake_gtest Testing Framework` --references--> `test_brain_node`  [INFERRED]
  AGENTS.md → src/buddy_brain/CMakeLists.txt
- `ament_cmake_gtest Testing Framework` --references--> `test_audio_node`  [INFERRED]
  AGENTS.md → src/buddy_audio/CMakeLists.txt
- `Dual-Brain Pipeline Flow` --references--> `Dual-Brain Timing Principle`  [INFERRED]
  CLAUDE.md → docs/communication_protocol.md
- `VisionPipelineNode` --implements--> `vision_component Library`  [EXTRACTED]
  docs/vision_architecture.md → src/buddy_vision/CMakeLists.txt
- `Architecture Simplification (Phase 0)` --references--> `brain_component Library`  [EXTRACTED]
  docs/plan.md → src/buddy_brain/CMakeLists.txt

## Hyperedges (group relationships)
- **Voice Trigger Pipeline** — audio_pipeline_node_audiopipelinenode, brain_node_brainnode, cloud_client_node_cloudclientnode, local_llm_node_localllmnode [EXTRACTED 1.00]
- **Emotion Trigger Pipeline** — vision_pipeline_node_visionpipelinenode, brain_node_brainnode, cloud_client_node_cloudclientnode [EXTRACTED 1.00]
- **Dual Brain Inference (Local + Cloud)** — brain_node_brainnode, local_llm_node_localllmnode, cloud_client_node_cloudclientnode [EXTRACTED 1.00]
- **Dual-Brain Inference Pipeline** — cmake_brain_brain_component, cmake_cloud_cloud_component, cmake_local_llm_local_llm_component [EXTRACTED 1.00]
- **Vision Capture and Inference Pipeline** — vision_architecture_cameraworker, vision_architecture_framebuffer, vision_architecture_modelinterface [EXTRACTED 1.00]
- **Audio-Brain Conversation Loop** — cmake_audio_audio_component, cmake_brain_brain_component, cmake_interfaces_inferencerequest [INFERRED 0.95]

## Communities (47 total, 35 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.07
Nodes (41): ament_cmake_gtest Testing Framework, class_loader Dynamic Loading, Dual-Brain Pipeline Topology, Single Container Deployment, Dual-Brain Pipeline Flow, buddy_main Executable, audio_component Library, AudioPipelineNode (+33 more)

### Community 1 - "Community 1"
Cohesion: 0.09
Nodes (23): CaptureImage.srv Generation, EmotionResult.msg Generation, ONNX Runtime Prebuilt Dependency, test_frame_buffer, test_model_interface, test_onnx_emotion_model, test_vision_node, vision_component Library (+15 more)

### Community 2 - "Community 2"
Cohesion: 0.2
Nodes (12): flush_sentence_buffer(), on_asr_text(), on_cloud_chunk(), on_emotion(), on_local_chunk(), on_tts_done(), on_wake_word(), process_chunk() (+4 more)

### Community 3 - "Community 3"
Cohesion: 0.21
Nodes (15): AudioPipelineNode, BrainNode, kComponents Registry, CameraConfig, CameraWorker, CloudClientNode, FrameBuffer, LocalLlmNode (+7 more)

### Community 4 - "Community 4"
Cohesion: 0.19
Nodes (4): call_doubao(), encode_image_base64(), json_escape(), on_inference_request()

### Community 5 - "Community 5"
Cohesion: 0.24
Nodes (4): discover_camera_names(), handle_capture(), load_camera_config(), on_configure()

### Community 8 - "Community 8"
Cohesion: 0.39
Nodes (5): argmax(), classify_emotion(), detect_face(), inference(), mat_to_tensor()

### Community 9 - "Community 9"
Cohesion: 0.7
Nodes (4): find_library(), find_param_file(), load_disabled_modules(), main()

### Community 11 - "Community 11"
Cohesion: 0.5
Nodes (5): BrainNode, BrainNode, BrainNodeTest, BrainNode, SegmentTest

## Knowledge Gaps
- **67 isolated node(s):** `FrameBuffer`, `MockModel`, `VisionPipelineNode`, `VisionNodeTest`, `CloudClientNode` (+62 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **35 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `brain_component Library` connect `Community 0` to `Community 1`, `Community 11`?**
  _High betweenness centrality (0.038) - this node is a cross-community bridge._
- **Why does `vision_component Library` connect `Community 1` to `Community 0`?**
  _High betweenness centrality (0.037) - this node is a cross-community bridge._
- **Why does `buddy_main Executable` connect `Community 0` to `Community 1`?**
  _High betweenness centrality (0.032) - this node is a cross-community bridge._
- **Are the 4 inferred relationships involving `brain_component Library` (e.g. with `buddy_main Executable` and `Brain Emotion Trigger Config`) actually correct?**
  _`brain_component Library` has 4 INFERRED edges - model-reasoned connections that need verification._
- **Are the 6 inferred relationships involving `vision_component Library` (e.g. with `test_vision_node` and `buddy_main Executable`) actually correct?**
  _`vision_component Library` has 6 INFERRED edges - model-reasoned connections that need verification._
- **Are the 4 inferred relationships involving `cloud_component Library` (e.g. with `buddy_main Executable` and `Doubao Cloud API Config`) actually correct?**
  _`cloud_component Library` has 4 INFERRED edges - model-reasoned connections that need verification._
- **Are the 3 inferred relationships involving `local_llm_component Library` (e.g. with `buddy_main Executable` and `Ollama Local LLM Config`) actually correct?**
  _`local_llm_component Library` has 3 INFERRED edges - model-reasoned connections that need verification._