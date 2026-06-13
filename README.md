ok# Ronin Kernel: Mobile AI Runtime Spine

![Build Status](https://img.shields.io/github/actions/workflow/status/intellibits28/ronin/build.yml?branch=main&style=flat-square)
![Version](https://img.shields.io/badge/version-4.7.27.05.27--HARDENED--v4.0-blue?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Android%20(SD778G%2B)-green?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)

**Ronin Kernel** is a sovereign, modular AI agent runtime optimized for Android (Snapdragon 778G+ / Mid-range). It utilizes the **Hardened v4.0 Production Architecture**, featuring dual-process isolation, OkHttp networking, and a 23k+ word Trie-based Myanmar segmenter for superior edge intelligence.

---

## 🧪 BETA TESTING IS ACTIVE!
We are currently in a public Beta phase. If you want to try Ronin on your device, please follow our:
👉 **[BETA TESTING GUIDE](docs/BETA_TESTING.md)** 👈

---

## 🏗️ Architecture & Blueprints
Explore the detailed technical foundations of Ronin:
*   **[Core Cognitive Blueprint v1.3](docs/BLUEPRINT_V1_3.md)**
*   **[Hardened Architecture Blueprint](docs/HARDENED_ARCH_V3.pdf)**
*   **[Memory Model Spec v2.1](docs/MEMORY_MODEL_V2.md)**
*   **[Sensor DSP Tool Spec v1.0](docs/SENSOR_DSP_V1.md)**
*   **[Technical Specifications](docs/TECHNICAL_SPECS.md)**
*   **[Project Manifest](docs/MANIFEST.md)**

---

## 📖 Description
Ronin Kernel v4.0 is optimized for **Mid-range Stability** and **Myanmar Linguistic Precision**. It addresses critical limitations in token handling and UI responsiveness.

### Key Features (Hardened v4.0)
*   **Dual-Process Isolation:** Inference runs in a separate `:inference_core` process, ensuring the UI stays buttery smooth even under heavy load.
*   **1024 Token Default:** Optimized for Myanmar UTF-8 text. Slider allows up to **2048 tokens** for extended reasoning.
*   **OkHttp Networking:** Robust cloud provider integration replacing legacy stacks.
*   **Trie-based BWS:** Pure C++ segmenter with 23k+ words for precise memory recall.
*   **Reactive Gemma 4 Streaming:** Zero-lag token rendering via reactive state.

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
*   **Architect:** intellibits28, Gemini CLI (Auto-Edit Mode)
*   **Inspiration:** Ronin Kernel v3.0 Hardened Architecture Blueprint.
