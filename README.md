# Ronin Kernel: Mobile AI Runtime Spine

![Build Status](https://img.shields.io/github/actions/workflow/status/intellibits28/ronin/build.yml?branch=main&style=flat-square)
![Version](https://img.shields.io/badge/version-4.7.27.05.26--HARDENED-blue?style=flat-square)
![Platform](![Platform](https://img.shields.io/badge/platform-Android%20(SD778G%2B)-green?style=flat-square))
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)

**Ronin Kernel** is a sovereign, modular AI agent runtime optimized for Android (Snapdragon 778G+). It utilizes the **Hardened v3.6 Production Architecture**, bridging pure C++20 reasoning spines with Kotlin hardware nodes via a zero-lag **Native Direct Bridge** for secure, high-performance edge intelligence.

---

## 🧪 BETA TESTING IS ACTIVE!
We are currently in a public Beta phase. If you want to try Ronin on your device, please follow our:
👉 **[BETA TESTING GUIDE](BETA_TESTING_GUIDE.md)** 👈

---

## 📖 Description
Ronin Kernel v3.6 introduces **Real-time Reactive Streaming** and a streamlined **Smart Cloud Setup**. It eliminates message display lag and provides an effortless way to manage multiple cloud providers (Gemini, OpenAI, OpenRouter).

### Key Features (Hardened v3.6)
*   **Reactive Gemma 4 Streaming:** Sub-second token rendering using reactive state properties. Watch Ronin reason and reply in real-time.
*   **Smart Cloud Configuration:** Simplified provider setup. Just enter your API key; endpoints and technical fields are pre-filled.
*   **Dynamic Model Fetching:** Fetch and select models directly from the cloud provider's API.
*   **Cloud Only Mode:** Option to force all inference to the cloud, preserving local device resources.
*   **Atomic Crash-Safe Persistence:** `CheckpointEngine` ensures your kernel state is safely persisted using shadow buffers.
*   **Advanced Sampling (T,P,K):** Granular control over Temperature, Top-K, and Top-P for the reasoning engine.
*   **Unified Single-Process Architecture:** Direct JNI bridge for maximum stability and performance on Snapdragon 778G+.

---

## 🏗️ Hardened Architecture

Ronin Kernel utilizes a **Unified Single-Process Model**:
*   **Kernel Core (C++20):** Manages Intent Routing, Memory Database, and Cognitive Orchestration.
*   **Neural Spine (Kotlin/JNI):** Leverages **LiteRT-LM 0.12.0** for on-device Gemma 4 inference with Direct JNI callback streaming.
*   **Persistence Layer:** SQLite FTS5 for keyword retrieval and encrypted Preferences for secure API keys.

---

## 📄 API Documentation

### JNI Bridge (`NativeEngine.kt`)
| Method | Description | Return |
| :--- | :--- | :--- |
| `initializeAsync()` | Loads native libraries and initializes the Kernel Core. | `Unit` |
| `processInputAsync(input)` | Executes the cognitive loop (Intent -> Local LM -> UI). | `String` |
| `loadModel(path)` | Hydrates the Gemma 4 reasoning spine using LiteRT-LM. | `Boolean` |
| `performCloudInferenceAsync()` | Executes secure off-device reasoning on Background IO thread. | `String` |
| `fetchAvailableModels()` | Dynamically fetches Cloud reasoning options via API. | `FetchResult` |

---

## 🧪 Testing & Diagnostics

### Real-time Logs
Monitor kernel decision-making via the in-app **Reasoning Console** (Cyan logs) or Logcat:
`adb logcat -s RoninKernel_Native:V RoninKernel_Worker:V`

### Host-side Verification
```bash
mkdir build_host && cd build_host
cmake -DCMAKE_BUILD_TYPE=Debug ..
make ronin_atomic_test && ./ronin_integration_test
```

---

## 📜 License
Ronin Kernel is released under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## 👥 Authors & Acknowledgments
*   **Architect:** Gemini CLI (Auto-Edit Mode)
*   **Inspiration:** Ronin Kernel v3.0 Hardened Architecture Blueprint.
