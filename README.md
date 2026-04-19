# Ronin Kernel

**Ronin Kernel** is a modular, high-efficiency AI agent runtime optimized for Android (Snapdragon 778G+). It utilizes a Hybrid Intent System and clean-room minimalist design patterns to provide a low-latency, privacy-first reasoning engine.

## 🚀 Tech Stack

- **Engine Core:** Pure C++20 (Android NDK) for deterministic execution and peak performance.
- **Hardware Interaction:** Kotlin-JNI Wrappers & Bridges for secure, asynchronous access to Android hardware APIs.
- **Inference:** ONNX Runtime (LiteRT) for local semantic routing and intent classification.
- **Build System:** CMake + Gradle for cross-platform portability and CI/CD automation.

## 🏗️ Architecture

### Hybrid Intent System
Ronin employs a tiered approach to intent resolution:
1. **Strict Bypass:** Immediate routing for unambiguous hardware commands (e.g., "Flashlight", "WiFi", "BT") to ensure near-zero latency.
2. **Semantic ONNX Router:** Vector-based reasoning for complex queries, mapping user intent to discrete capability nodes.
3. **Reasoning Fallback:** Graceful degradation to general chat or clarification when confidence thresholds are not met.

### Phase 4.0: Modular Evolution
We are currently transitioning to a **Vtable-based Registry**. This evolution decouples the `IntentEngine` from specific hardware implementations using `BaseSkill` interfaces, inspired by the NullClaw Component-Interface pattern.

## 🧠 Memory & System Integrity

- **LMK-Awareness:** OS-driven Memory Guard via ComponentCallbacks2.onTrimMemory(). The kernel automatically prunes KV caches and triggers L3 persistence when memory pressure exceeds 85%.
- **Zero-Copy Access:** Utilizes DirectByteBuffers for efficient data transfer between Kotlin and the C++ reasoning spine.
- **Global Reference Management:** Rigorous JNI lifecycle management to prevent memory leaks and ensure thread safety across detached hardware threads.
- **Thermal Guard:** Dynamic Thermal Throttling (Step-down generation speed at 40°C, unload at critical limits).

## 🛠️ Status (v3.9.7-RECOVERY)

- [x] **Verified JNI Thread Safety:** Detached threads with proper `AttachCurrentThread()` pointer casting.
- [x] **Real Hardware Integration:** Physical toggling for Bluetooth and WiFi (Android 10+ panel fallback).
- [x] **Asynchronous GPS Bridge:** Real-time location injection from `FusedLocationProviderClient` into the C++ Reasoning Spine.
- [x] **Search Privacy Guard:** Strict extension isolation and exclusion of system files (.env, .ignore).

## 🧪 Testing

- **Robolectric:** Unit testing for UI auto-scroll logic and SQLite persistence.
- **GoogleTest:** C++ core logic verification for memory integrity and search precision.

---
*Clean-Room Implementation | Minimalist Runtime Efficiency*
