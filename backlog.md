# Ronin Kernel Task Backlog

## Priority 1: Phase 4.5 (Dual-Process & Command Intelligence)
- [ ] **Process Isolation:** Implement `:inference_core` process split via Android Service.
- [ ] **Binder IPC:** Create AIDL/Binder interface for cross-process reasoning requests.
- [ ] **mmap Hydration:** Replace standard File IO with `mmap` (MAP_PRIVATE) for LiteRT-LM.
- [ ] **Command UI:** Implement Suggester Popup for `/` commands and OnTextChangedListener logic.
- [ ] **Model Manager:** Restore Radio-button list UI with deletion and hydration status (green/red).

## Priority 2: Phase 4.5.x (Sensory Hub)
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
