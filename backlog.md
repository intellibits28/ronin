# Ronin Kernel Task Backlog

## Priority 1: Phase 12.0 (Sensor Intelligence & Hardening)
- [ ] **ASensorManager Bridge:** Implement native C++ bridge for high-frequency IMU (Gyro/Accel) data.
- [ ] **DSP Offloading:** Explore Qualcomm Hexagon SDK for low-power sensor pre-processing.
- [ ] **Vibration Node:** Implement structural health monitoring skill using haptic feedback.
- [ ] **Context Pruning:** Refine the 0.8GB RAM Guard to selectively prune KV-cache chunks instead of full reset.

## Priority 2: Phase 13.0 (Social Mesh & Encryption)
- [ ] **Sovereign Identity:** Implement decentralized ID (DID) for kernel ownership.
- [ ] **Encrypted Contacts:** Secure JNI bridge for local contact/messaging analysis.
- [ ] **Multi-turn Memory:** Enhance FTS5 keywords with temporal relevance scores.

## Completed (Hardened v3.0 Production)
- [x] **Native Direct Bridge:** Implemented single-process JNI callbacks for zero-lag streaming.
- [x] **Lexical Intent Spine:** Strict token-based hardware matching in `IntentEngine.cpp`.
- [x] **Error 13 Fix:** Instruction isolation and prefill buffer optimization for SD778G+.
- [x] **Thinking Filter:** prunning reasoning tokens before SQLite persistence.
- [x] **Storage Optimization:** Filename resolution and compiled-cache utility active.
- [x] **CalVer Integration:** System-wide versioning update to v4.7.26.05.24.

## Completed (Phase 11.0 Stabilization)
- [x] **LiteRT-LM 0.12.0:** Successful migration to the latest reasoning engine.
- [x] **Myanmar Reasoning:** Enforced Burmese thinking and reply constraints.
- [x] **Cyan Console:** Improved visibility and styling for reasoning logs.
- [x] **Cloud Hardening:** Descriptive error reporting and auto-fallback logic.

*Last Updated: May 25, 2026*
