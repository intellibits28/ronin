# Ronin Kernel Task Backlog

## Priority 1: Phase 4.4.8 (Logic Restoration)
- [ ] **Real Inference Injection:** Replace hardcoded placeholders with real `LlmInferenceAPI` token extraction.
- [ ] **Gemma Prompt Wrapping:** Implement official Gemma chat templates for local reasoning.
- [x] **Cloud Bridge Fixed:** Correct the Gemini 1.5 Pro endpoint URL and JSON schema to resolve 404 errors.
- [x] **Naypyidaw Patch (Thermal):** Implement aggressive NPU throttling (Low-power mode + 64 max tokens) at >= 43°C.

## Priority 2: Phase 4.4.9 (Sensory Hub)
- [ ] **Native JNI Sensors:** Implement `ASensorManager` bridge for IMU/Vibration access.
- [ ] **SHM Node:** Develop specialized skill for vibration-based structural health monitoring.

## Priority 3: Future Roadmap (v4.5+)
- [ ] **v4.5 (SOCIAL-BRIDGE):** Contacts/SMS integration.
- [ ] **v4.6 (DSP-SKILL):** Audio signal processing node.

## Completed (v4.4 & Prior)
- [x] **Thermal Guard Fixed:** Implemented Stability Guard (42°C) and Naypyidaw Patch (43°C) for hardware safety.
- [x] **Cloud Bridge Fixed:** Corrected Gemini 1.5 Pro endpoint formatting and JSON escaping.
- [x] **Path Mapping Fixed:** Resolved desync between Router (.onnx) and Reasoning (.bin) paths in UI.
- [x] **v4.4 Dynamic Config:** UI Settings, Cloud Manifest JSON, and Terminal Command Interface.
- [x] **v4.3 LiteRT-LM Integration:** Specialized Gemma 4 reasoning spine and Cloud escalation.
- [x] **v4.2 NPU Acceleration:** Hierarchical NNAPI routing for SD778G.
- [x] **v4.1 Hardware Reality:** Real GPS coordinates and LMK-survival re-awakening.
- [x] **v4.0 Unified Interface:** Vtable-based Skill Registry (All nodes inherit from `BaseSkill`).
- [x] **Push-Based Memory Guard:** Native `onTrimMemory` hooks for LMK avoidance.

*Last Updated: April 21, 2026*
