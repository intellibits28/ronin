# Ronin Kernel Task Backlog

## Priority 1: Critical Fixes & Calibration
- **Intent ID Mapping Fix:** Investigate and correct `IntentEngine.cpp`. Resolve issue where 'Bluetooth' triggers 'Flashlight'.
- **File Search Filter Fix:** Update `FileSearchNode` or relevant logic to ensure strict extension checking (.mp3 only returns audio, .pdf only returns documents).

## Priority 2: Phase 4.0 & 4.1 (Modular Evolution)
- **Modular Skill Interface:** Transition to a Vtable-based Registry.
- **Vtable Registry Implementation:** Decouple `IntentEngine` from specific hardware calls using `BaseSkill` interfaces.
- **Local Brain (Gemma 4):** Integration of MediaPipe LLM Inference API (LiteRT).
- **Thermal & Battery Guard:** JNI monitor for LLM unloading based on temperature/battery.

## Priority 3: Future Roadmap (v4.2 - v4.4)
- **v4.2 (SENSOR-HUB):** Native JNI SensorEventListener for Vibration Analysis.
- **v4.3 (SOCIAL-BRIDGE):** `ContactNode` & `SmsNode` for multi-step tasks.
- **v4.4 (DSP-SKILL):** Digital Signal Processing for sensor/audio filtering.

*Generated: April 15, 2026*