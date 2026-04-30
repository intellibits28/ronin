# Ronin Kernel Task Backlog

## Priority 1: feature/hydration-fix
- [ ] **Kernel Debug:** Trace `LlmInference::Create` failure on physical device.
- [ ] **ABI Alignment:** Verify MediaPipe binary compatibility with Snapdragon 778G NPU.

## Priority 2: Phase 4.5.x (Sensory Hub)
- [ ] **Native JNI Sensors:** Implement `ASensorManager` bridge for IMU/Vibration access.
- [ ] **SHM Node:** Develop specialized skill for vibration-based structural health monitoring.

## Completed (v4.5-alpha-2026.04.30)
- [x] **File Search (v5.15):** Implemented semantic search with background indexing, media support, and /more pagination.
- [x] **JNI Spine Rearchitecture:** Aligned with official LiteRT-LM patterns and thread-safe RAII utilities.
- [x] **Dynamic Cloud Providers:** Resolved 404s and enabled custom endpoints in settings.
- [x] **Command Routing:** Intercepted /more, /skills, /status in the unified kernel process loop.

## Completed (Legacy v4.8.1 Patch)
- [x] **Startup Fix:** Resolved race condition between asset synchronization and JNI kernel initialization.
- [x] **Unified Hydration:** Refactored InferenceEngine to manage both Core Router (.onnx) and Reasoning (.bin) brains.
- [x] **Zero-Mock Policy (Rule 6):** Removed all hardcoded system echo strings.

## Completed (Legacy v4.4 & Prior)
- [x] **v4.4 Dynamic Config:** UI Settings, Cloud Manifest JSON, and Terminal Command Interface.
- [x] **v4.3 LiteRT-LM Integration:** Specialized Gemma 4 reasoning spine and Cloud escalation.
- [x] **v4.2 NPU Acceleration:** Hierarchical NNAPI routing for SD778G.
- [x] **v4.0 Unified Interface:** Vtable-based Skill Registry.
- [x] **Push-Based Memory Guard:** Native `onTrimMemory` hooks for LMK avoidance.

*Last Updated: April 30, 2026*
