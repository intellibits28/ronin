# Ronin Kernel: Mobile AI Runtime Spine

![Build Status](https://img.shields.io/github/actions/workflow/status/intellibits28/ronin/build.yml?branch=feature/hydration-fix&style=flat-square)
![Version](https://img.shields.io/badge/version-4.7.26.05.25--HARDENED-blue?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Android%20(SD778G%2B)-green?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)

**Ronin Kernel** is a sovereign, modular AI agent runtime optimized for Android (Snapdragon 778G+). It utilizes the **Hardened v3.2 Production Architecture**, bridging pure C++20 reasoning spines with Kotlin hardware nodes via a zero-lag **Native Direct Bridge** for secure, high-performance edge intelligence.

---

## 📖 Description
Ronin Kernel v3.2 introduces a redesigned **AI Edge Gallery Style UI**, featuring real-time **Token Streaming** and intelligent **Reasoning Log filtering**. It eliminates transport bottlenecks and provides deep control over model parameters.

### Key Features (Hardened v3.2)
*   **Real-time Token Streaming:** Watch Ronin think and reply in real-time with zero-wait UI updates.
*   **Thinking Filter:** Automatically routes `[THINK]` tokens to the Reasoning Console (Cyan) while displaying the `[REPLY]` in the chat bubble.
*   **AI Edge Gallery UI:** Redesigned layout with Settings Drawer, Conversation History, and Editable System Prompt.
*   **Dynamic Engine Control:** Adjust Max Tokens (SD778G+ optimized default: 768) and system instructions on the fly.
*   **Native Direct Bridge:** Direct JNI callbacks for SHM-speed streaming and stable multi-turn logic.
*   **Single Gemma 4 Spine:** Optimized prefill stability and 1.1GB RAM Guard to prevent Error 13.


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
