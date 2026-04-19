# Ronin Kernel Task Backlog

## Priority 1: Phase 4.1 (Local Brain & Guards)
- [ ] **Local Brain (Gemma 4):** Integration of MediaPipe LLM Inference API (LiteRT).
- [ ] **Thermal & Battery Guard:** Dynamic Thermal Throttling (Step-down generation speed at 40°C, unload at critical limits).
- [ ] **Data Protocol Optimization:** Transition JNI payloads from raw strings to structured JSON.

## Priority 2: Future Roadmap (v4.2 - v4.4)
- [ ] **v4.2 (SENSOR-HUB):** Native JNI SensorEventListener for Vibration Analysis.
- [ ] **v4.3 (SOCIAL-BRIDGE):** `ContactNode` & `SmsNode` for multi-step tasks.
- [ ] **v4.4 (DSP-SKILL):** Digital Signal Processing for sensor/audio filtering.

## Completed (Phase 4.0 & Prior)
- [x] **v4.0 Unified Interface:** Transition to Vtable-based Skill Registry (All nodes inherit from `BaseSkill`).
- [x] **Asset Synchronization:** Automatic extraction of models and manifests from APK to storage.
- [x] **Hardware Bridge Fix:** Restored JNI actuation for GPS, WiFi, and Bluetooth.
- [x] **Intent ID Mapping Fix:** Resolved hardware routing conflicts via Strict Bypass.
- [x] **File Search Filter Fix:** Implemented strict extension isolation.
- [x] **UI Auto-Scroll & History:** Paginated chat history and automatic scroll to bottom.

*Last Updated: April 19, 2026*
