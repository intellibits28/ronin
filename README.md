# Ronin Kernel: Mobile AI Runtime Spine

![Build Status](https://img.shields.io/github/actions/workflow/status/intellibits28/ronin/build.yml?branch=feature/hydration-fix&style=flat-square)
![Version](https://img.shields.io/badge/version-4.1.1--AUDIT--ALIGNED-blue?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Android%20(SD778G%2B)-green?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)

**Ronin Kernel** is a modular, high-efficiency AI agent runtime optimized for Android (Snapdragon 778G+). It bridges pure C++20 reasoning spines with Kotlin hardware bridges, utilizing **Phase 4.5 Dual-Process Isolation** to enable secure, non-blocking AI agency on the edge.

---

## 📖 Description

Ronin Kernel solves the "Latency vs. Privacy" trade-off by implementing a **Local Inference-as-a-Service** architecture. By separating the UI from the reasoning spine into distinct processes, it ensures zero UI-lag during heavy LLM (Gemma 4) inference.

### Key Features
*   **Dual-Process Isolation:** UI/Indexing and Inference Engine run in separate processes linked via Binder IPC.
*   **Command Intelligence:** Real-time Suggester Popup and Auto-completion for `/` terminal commands.
*   **Tiered Intent Routing:** NPU-accelerated semantic classification with 1.0 confidence hardware bypass.
*   **mmap Hydration:** Memory-mapped I/O for near-instant model loading and reduced RAM footprint (No FileStreams).
*   **High-Speed Staging:** 1MB transfer buffers for large model migration from User to Internal storage.
*   **System Guards:** Active Thermal Throttling (**Safe Mode at 42°C**) and LMK-aware memory pruning.

---

## 🏗️ Architecture

Ronin Kernel utilizes a **Service-Oriented Process Model**:
*   **Kernel Core:** Manages the UI, File Indexing, and JNI Bridge.
*   **Inference Spine (`:inference_core`):** A dedicated foreground service (`FOREGROUND_SERVICE_TYPE_SPECIAL_USE`) running the LiteRT-LM (Gemma 4) engine.
*   **Memory Layer:** Tri-anchor pruning (L1-RAM, L2-Cache, L3-SQLite) with `mmap` persistence.

---

## 📄 API Documentation

### JNI Bridge (`NativeEngine.kt`)
| Method | Description | Return |
| :--- | :--- | :--- |
| `initializeAsync()` | Loads native libraries and hydrates the spine on a worker thread. | `Unit` |
| `processInput(input)` | Executes tiered reasoning (Hardware -> Local LM -> Cloud). | `String` |
| `loadModel(path)` | Maps neural weights into memory via `mmap` for NPU usage. | `Boolean` |
| `checkFileAccess(path)` | Diagnostic probe to verify native-side file readability. | `String` |
| `notifyTrimMemory(level)` | Manages RAM pressure by stopping low-priority tasks. | `Unit` |

---

## 🧪 Testing

### Host-side (Linux x64)
Run C++ unit tests to verify memory and logic integrity:
```bash
mkdir build_host && cd build_host
cmake -DCMAKE_BUILD_TYPE=Debug ..
make ronin_atomic_test && ./ronin_atomic_test
```

### Android-side
Monitor hydration bottlenecks via Logcat:
`adb logcat -s RoninKernel_Native:V`

---

## 📜 License
Ronin Kernel is released under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## 👥 Authors & Acknowledgments
*   **Main Contributor:** Gemini CLI / IntelliBits
*   **Inspiration:** MediaPipe LLM Inference API, Qualcomm AI Stack.
