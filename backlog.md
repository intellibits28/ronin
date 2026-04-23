# Ronin Kernel Task Backlog

## Priority 1: Phase 4.5.x (Sensory Hub)
- [ ] **Native JNI Sensors:** Implement `ASensorManager` bridge for IMU/Vibration access.
- [ ] **SHM Node:** Develop specialized skill for vibration-based structural health monitoring.

## Priority 2: Future Roadmap (v4.6+)
- [ ] **v4.6 (SOCIAL-BRIDGE):** Contacts/SMS integration.
- [ ] **v4.7 (DSP-SKILL):** Audio signal processing node.

## Completed (v4.8.1 Patch)
- [x] **Startup Fix:** Resolved race condition between asset synchronization and JNI kernel initialization.
- [x] **Unified Hydration:** Refactored InferenceEngine to manage both Core Router (.onnx) and Reasoning (.bin) brains.
- [x] **Zero-Mock Policy (Rule 6):** Removed all hardcoded system echo strings.

## Completed (v4.4.9 / v4.5.0 Patch)
- [x] **Priority 1 (Phase 4.5.0):** Implement Async Model Loading with JNI status updates ("Kernel Hydrating...", "NPU Tensors Allocated...", "Kernel Ready.").
- [x] **Priority 2 (Phase 4.4.9):** Hard-wire Intent Routing for Greetings (hi, hello, မင်္ဂလာပါ) to bypass confidence loops.
- [x] **Priority 3 (Phase 4.4.9):** Replace placeholder replies with real `LlmInferenceAPI` token generation and streaming.
- [x] **Priority 4 (Phase 4.5.0):** Refactor Cloud Bridge with mandatory Gemini JSON schema and stable v1 endpoint to fix 404 errors.
- [x] **Priority 5 (Phase 4.5.0):** Restore State Persistence (Saving model paths and providers across app restarts using EncryptedSharedPreferences).

## Completed (Legacy v4.4 & Prior)
- [x] **Logic Restoration (4.4.8):** Real token extraction, Gemma templates, and Naypyidaw Patch.
- [x] **Cloud Bridge Fixed:** Initial correction of Gemini endpoint formatting and JSON escaping.
- [x] **Path Mapping Fixed:** Resolved desync between Router (.onnx) and Reasoning (.bin) paths in UI.
- [x] **v4.4 Dynamic Config:** UI Settings, Cloud Manifest JSON, and Terminal Command Interface.
- [x] **v4.3 LiteRT-LM Integration:** Specialized Gemma 4 reasoning spine and Cloud escalation.
- [x] **v4.2 NPU Acceleration:** Hierarchical NNAPI routing for SD778G.
- [x] **v4.1 Hardware Reality:** Real GPS coordinates and LMK-survival re-awakening.
- [x] **v4.0 Unified Interface:** Vtable-based Skill Registry (All nodes inherit from `BaseSkill`).
- [x] **Push-Based Memory Guard:** Native `onTrimMemory` hooks for LMK avoidance.

*Last Updated: April 21, 2026*
