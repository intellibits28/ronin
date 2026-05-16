# Ronin Kernel Task Backlog

## Priority 1: Phase 4.5 (Dual-Process & Command Intelligence)
- [x] **LiteRT-LM Migration:** Successful upgrade to v0.11.0 for native Gemma 4 support.
- [x] **Jinja Template Integration:** Alignment of PromptFactory with official Gemma 4 specifications.
- [x] **Memory Model v2.1 (Native Path):** Implemented SentencePiece + LiteRT C++ for direct MM indexing.
- [x] **Integrity Guard:** Native Magic Byte (TFL3) and size verification for models.
- [x] **Adaptive RAM Guard:** Size-aware hydration thresholds (1.0GB - 1.5GB).
- [ ] **SHM MAP_SHARED Audit:** Verify cross-process pointer consistency for zero-copy streaming.
- [ ] **Process Isolation:** Implement `:inference_core` process split via Android Service.
- [ ] **Binder IPC:** Create AIDL/Binder interface for cross-process reasoning requests.
- [ ] **Command UI:** Implement Suggester Popup for `/` commands and OnTextChangedListener logic.

## Priority 2: Phase 4.5.x (Sensory Hub & Quantization)
- [x] **Hybrid Precision:** Implemented Float16 for Semantic and INT8 for Episodic storage.
- [ ] **INT8 Tuning:** Optimize vector storage for 384-dim semantic memories.
- [ ] **Native JNI Sensors:** Implement `ASensorManager` bridge for IMU/Vibration access.
- [ ] **SHM Node:** Develop specialized skill for vibration-based structural health monitoring.

## Completed (Phase 4.0 Deep Audit)
- [x] **JNI Verification:** Fixed Health Data mapping (updateSystemHealth -> HardwareBridge).
- [x] **Native Access Probe:** Verified model file readability via JNI `fopen` (checkFileAccessNative).
- [x] **ProGuard Safety:** Added survival rules for JNI class and method names.
- [x] **Staging Speedup:** Implemented 1MB buffer for high-speed model import.

## Completed (v4.0 Unified Interface)
- [x] **Vtable Registry:** Decoupled skill execution from intent routing.
- [x] **File Search (v5.15):** Implemented semantic search with /more pagination.
- [x] **JNI Thread Safety:** Implemented named threads and async-to-sync CountDownLatch bridge.
- [x] **Dynamic Cloud:** Enabled custom Gemini endpoints via providers.json.

*Last Updated: May 3, 2026*
