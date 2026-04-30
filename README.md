# Ronin Kernel: Mobile AI Runtime Spine

![Build Status](https://img.shields.io/github/actions/workflow/status/intellibits28/ronin/build.yml?branch=dev-recovery-4.8.1&style=flat-square)
![Version](https://img.shields.io/badge/version-4.0.0--PRO--FINAL-blue?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Android%20(SD778G%2B)-green?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)

**Ronin Kernel** is a modular, high-efficiency AI agent runtime optimized for Android (Snapdragon 778G+). It bridges pure C++20 reasoning spines with Kotlin hardware bridges to enable deterministic, low-latency AI agency on the edge.

---

## 📖 Description

Ronin Kernel exists to solve the "Latency vs. Privacy" trade-off in mobile AI. By implementing a tiered **Hybrid Intent System**, it ensures that common hardware tasks execute with near-zero latency, while complex semantic queries are handled by a local LiteRT-LM (Gemma 4) reasoning spine.

### Key Features
*   **Tiered Intent Routing:** Combines strict keyword bypass with NPU-accelerated ONNX semantic classification.
*   **Unified Skill Registry:** Vtable-based interface (`BaseSkill`) for both cognitive tools (Embedding) and hardware tools (GPS, WiFi, BT).
*   **LiteRT-LM Integration:** Production-ready MediaPipe GenAI implementation with weight mapping and KV-cache management.
*   **System Guards:** Active Thermal Throttling and LMK-aware memory pruning using `ComponentCallbacks2`.
*   **Thread-Safe JNI:** RAII-based `ScopedJniEnv` for robust, asynchronous callbacks between C++ and Kotlin.

---

## 🗂️ Table of Contents
1. [Installation](#installation)
2. [Quick Start](#quick-start)
3. [Architecture](#architecture)
4. [API Documentation](#api-documentation)
5. [Configuration](#configuration)
6. [Development](#development)
7. [Testing](#testing)
8. [License](#license)

---

## ⚙️ Installation

### Prerequisites
*   **Android Studio Jellyfish+** or later.
*   **Android NDK r26b+**.
*   **Snapdragon 778G+** (Recommended for NPU acceleration).
*   **CMake 3.22.1+**.

### Step-by-Step
1.  **Clone the Repository:**
    ```bash
    git clone https://github.com/intellibits28/ronin.git
    cd ronin
    ```
2.  **Hydrate Model Assets:**
    Place your `.task` or `.bin` models in `android/app/src/main/assets/models/`.
3.  **Build with Gradle:**
    ```bash
    ./gradlew :app:assembleDebug
    ```
4.  **Verify:**
    Run the app and check Logcat for `SUCCESS: Native reasoning spines hydrated`.

---

## 🚀 Quick Start

### Minimal Native Usage
In your Kotlin activity or service:
```kotlin
val engine = NativeEngine(context)
engine.initialize()

// Async model loading
engine.loadModelAsync("/path/to/gemma_4.task")

// Process input through local reasoning spine
scope.launch {
    val response = engine.processInputAsync("Turn on the flashlight and find my location")
    println(response)
}
```

---

## 🏗️ Architecture

Ronin Kernel utilizes a **Zero-Mock Policy**:
*   **Reasoning Spine:** autoregressive Gemma decoding via MediaPipe C++ API.
*   **Intent Layer:** 1.0 confidence bypass for hardware IDs.
*   **Memory Layer:** Tri-anchor pruning (L1-RAM, L2-Cache, L3-SQLite).

---

## 📄 API Documentation

### JNI Bridge (`NativeEngine.kt`)
| Method | Description | Return |
| :--- | :--- | :--- |
| `initialize()` | Hydrates the C++ Kernel and links hardware callbacks. | `Unit` |
| `processInput(input)` | Executes tiered reasoning (Hardware -> Local LM -> Cloud). | `String` |
| `loadModel(path)` | Maps neural weights into memory for NPU usage. | `Boolean` |
| `notifyTrimMemory(level)` | Manages LMK pressure by pruning non-essential buffers. | `Unit` |
| `injectLocation(lat, lon)` | Synchronizes real-world coordinates with the kernel. | `Unit` |

---

## 🛠️ Configuration

### Environment Variables (Build Time)
*   `ANDROID_ABI`: Set to `arm64-v8a` for Snapdragon performance.
*   `ANDROID_STL`: Defaults to `c++_shared`.

### Runtime Registry
Providers and models are managed via a JSON manifest synced to the kernel through `updateModelRegistry(json)`.

---

## 🧪 Testing

### Host-side (Linux x64)
Run C++ unit tests to verify memory and logic integrity:
```bash
mkdir build_host && cd build_host
cmake ..
make ronin_atomic_test && ./ronin_atomic_test
```

### Android-side
Instrumented tests verify JNI linkages:
```bash
./gradlew connectedDebugAndroidTest
```

---

## 📜 License
Ronin Kernel is released under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## 👥 Authors & Acknowledgments
*   **Main Contributor:** Gemini CLI / IntelliBits
*   **Inspiration:** MediaPipe LLM Inference API, Qualcomm AI Stack.

---

## 🆘 Support & Contact
*   **Issues:** [GitHub Issue Tracker](https://github.com/intellibits28/ronin/issues)
*   **Community:** Phase 4.8 Stable Discussion.
