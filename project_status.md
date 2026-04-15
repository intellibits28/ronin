# Ronin Kernel: Project Status & Strategic Roadmap

## 1. Project Overview
**Name:** Ronin Kernel (v3.9 -> v4.0 Evolution)
**Objective:** A modular, high-efficiency AI agent runtime optimized for Snapdragon 778G, utilizing a Hybrid Intent System and Clean-Room minimalist design patterns.

---

## 2. Current Status (v3.9.4-STABLE)
- [x] **Tier 1 (Greetings/Keywords):** Fully functional. Tier 1 (Greetings) and Tier 2 (Dynamic Keywords) are decoupled and order-independent.
- [x] **Hardware Toggles:** Flashlight, WiFi, and Bluetooth fixed. Integrated **Async std::async** execution to prevent UI freezes.
- [x] **Safety Logic:** Negation priority implemented. 'OFF/Stop/Disable' tokens correctly override all positive triggers.
- [x] **Android Integration:** Critical hardware permissions (WiFi, BT, GPS, Camera) added to Manifest. **Runtime Permission Guard (v3.9.4)** implemented for Android 12+.
- [x] **State Management:** SQLite-based chat history and Context Awareness (`m_last_suggested_subject`) are stable and survive Activity rotation via ViewModel.
- [x] **Tier 3 (ONNX Inference):** Bridge integrated and active. Model loading verified via JNI logs. **Diagnostic Logging (v3.9.4)** implemented to capture model confidence.

---

## 3. Stabilization Phase (Completed)
- [x] **Fix ANR (v3.9.2):** COMPLETED. Moved WiFi/Bluetooth toggles to background threads using `std::async(std::launch::async)`.
- [x] **Negation Priority:** COMPLETED. 'OFF' tokens now have absolute override authority in `IntentEngine.cpp`.
- [x] **Permission Guard (v3.9.4):** COMPLETED. Implemented runtime request logic for BLUETOOTH_CONNECT and SCAN.
- [x] **Diagnostic Logging (v3.9.4):** COMPLETED. Added try-catch blocks and __android_log_print for hardware calls.

---

## 4. Phase 4.0: The Modular Evolution (NullClaw Patterns)
**Core Redesign Goals:**
- **Modular Skill Interface:** Transition to a **Vtable-based Registry**. Use a Component-Interface pattern to allow adding new Skills (GPS, SMS, Sensors) without recompiling the core kernel.
- **Vtable Registry Implementation:** Decouple `IntentEngine` from specific hardware calls using `BaseSkill` interfaces.

---

## 5. Phase 4.1: Local Inference Engine (Brain Plugins)
- **Local Brain (Gemma 4):** Integration of **MediaPipe LLM Inference API (LiteRT)** for on-demand local reasoning.
- **Multimodal Foundation:** Placeholder JNI structures for Vision (Camera2) and AudioRecord (Whisper).
- **Thermal & Battery Guard:** Implementation of a JNI-level monitor that unloads the Local LLM if device temperature > 40°C or battery < 15%.

---

## 6. Future Skills & Capabilities (The Roadmap)
- **v4.2 (SENSOR-HUB):** Native JNI SensorEventListener for SHM (Vibration Analysis).
- **v4.3 (SOCIAL-BRIDGE):** ContactNode & SmsNode for multi-step tasks (e.g., "Send location to Dad").
- **v4.4 (DSP-SKILL):** Digital Signal Processing for sensor noise reduction and audio filtering.

---

## 7. Divergence from Original Plan
- **Intelligence Shift**: Transitioned from a rule-engine to a neural-hybrid design midway through v3.8.
- **Persistence Layer**: Added native SQLite history to support complex multi-turn context beyond the original volatile scope.
- **Async Execution**: Pivoted to mandatory `std::async` wrappers for all hardware calls to ensure strict 60FPS UI stability.

*Last Updated: April 15, 2026*
