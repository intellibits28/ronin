# Ronin Kernel: Project Status & Strategic Roadmap

## 1. Project Overview
**Name:** Ronin Kernel (v3.9.4 -> v4.0 Evolution)
**Objective:** A modular, high-efficiency AI agent runtime optimized for Snapdragon 778G, utilizing a Hybrid Intent System and Clean-Room minimalist design patterns.

---

## 2. Current Status (v3.9.4-ASYNC-STABLE)
- [x] **v3.9.3 (PERMISSION-FIX):** Critical hardware permissions added to Manifest.
- [x] **v3.9.4 (ONNX-LINKED):** libonnxruntime.so verified as correctly linked via GitHub Action Run ID 24439395884.
- [x] **Safety Logic:** Negation priority implemented. 'OFF' tokens correctly override all positive intents.
- [x] **Async Hardware Bridge:** JNI `triggerHardwareAction` updated for asynchronous execution.

---

## 3. Active Stabilization (The "Sanitization" Phase)
- [ ] **JNI Thread Safety (v3.9.5):** Resolve Bluetooth 'Instant Exit' by properly attaching background threads to Java VM via `AttachCurrentThread()`.
- [ ] **Tier 2 Verification:** Implement detailed JNI logging in `IntentEngine.cpp` to print `Intent ID` and `Confidence Score` to verify ONNX activity.
- [ ] **Memory Guard:** Implement JNI-level logging for RAM usage during high-frequency hardware toggles to monitor memory pressure.

---

## 4. Phase 4.0: The Modular Evolution (NullClaw Patterns)
- **Modular Skill Interface:** Transition to a **Vtable-based Registry**.
- **Vtable Registry Implementation:** Decouple `IntentEngine` from specific hardware calls using `BaseSkill` interfaces (Inspired by NullClaw Component-Interface pattern).

---

## 5. Phase 4.1: Local Inference Engine (Brain Plugins)
- **Local Brain (Gemma 4):** Integration of **MediaPipe LLM Inference API (LiteRT)** for on-demand local reasoning.
- **Thermal & Battery Guard:** Implementation of a JNI monitor that unloads the Local LLM if device temperature > 40°C or battery < 15%.

---

## 6. Future Skills & Capabilities (The Roadmap)
- **v4.2 (SENSOR-HUB):** Native JNI SensorEventListener for Vibration Analysis (SHM).
- **v4.3 (SOCIAL-BRIDGE):** `ContactNode` & `SmsNode` for multi-step tasks (e.g., "Send location to Dad").
- **v4.4 (DSP-SKILL):** Digital Signal Processing for sensor/audio filtering.

---

## 7. Compliance & Design Credits
- Core logic utilizes clean-room design patterns for minimalist runtime efficiency (Inspired by NullClaw principles: Static Dispatch, Unmanaged Structures, Circular Buffers).

*Last Updated: April 15, 2026*
