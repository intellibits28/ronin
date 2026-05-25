# Ronin Kernel: Mobile AI Runtime Spine

![Build Status](https://img.shields.io/github/actions/workflow/status/intellibits28/ronin/build.yml?branch=feature/hydration-fix&style=flat-square)
![Version](https://img.shields.io/badge/version-4.7.26.05.24--HARDENED-blue?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Android%20(SD778G%2B)-green?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)

**Ronin Kernel** is a sovereign, modular AI agent runtime optimized for Android (Snapdragon 778G+). It utilizes the **Hardened v3.0 Production Architecture**, bridging pure C++20 reasoning spines with Kotlin hardware nodes via a zero-lag **Native Direct Bridge** for secure, high-performance edge intelligence.

---

## 📖 Description
Ronin Kernel v3.0 eliminates transport bottlenecks by integrating the UI and Inference Engine into a single-process execution model. It features a hardened **Lexical Intent Spine** for precise tool-calling and **LiteRT-LM 0.12.0** (Gemma 4) for deep semantic reasoning.

### Key Features (Hardened v3.0)
*   **Native Direct Bridge:** Direct JNI callbacks replace legacy AIDL/IPC, enabling SHM-speed token streaming and stable multi-turn logic.
*   **Single Gemma 4 Spine:** Optimized for Snapdragon 778G+ with a hard 512-token cap to ensure stable prefill buffers.
*   **Lexical Intent Spine:** Strict token-based hardware routing (GPS, Flashlight, etc.) via SQLite FTS5 keywords, preventing fuzzy misrouting.
*   **RAM Guard:** Real-time LMK-aware KV-cache pruning triggered at 0.8GB free RAM.
*   **Thinking Filter:** Automatic pruning of reasoning tokens (`[THINK]`) before chat history persistence to prevent context poisoning.
*   **Myanmar Reasoning:** Deep support for Burmese logic, reasoning, and response generation.
*   **Storage Optimization:** Real filename resolution and compiled-cache purging utility to minimize app footprint.


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
| `nativeResetContext()` | Synchronously prunes native memory and model KV-cache. | `Unit` |
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
make ronin_atomic_test && ./ronin_atomic_test
```

---

## 📜 License
Ronin Kernel is released under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## 👥 Authors & Acknowledgments
*   **Architect:** Gemini CLI (Auto-Edit Mode)
*   **Inspiration:** Ronin Kernel v3.0 Hardened Architecture Blueprint.
