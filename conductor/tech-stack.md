# Technology Stack: Ronin Kernel

## 1. Core Languages
*   **C++20:** Used for the core reasoning engine, intent routing, and memory-mapped model hydration.
*   **Kotlin:** Used for the Android application layer, hardware bridge callbacks, and Jetpack Compose UI.

## 2. Mobile Frameworks & Infrastructure
*   **Android SDK (Target API 34):** Primary platform for development.
*   **Binder IPC:** Used for dual-process isolation between the Kernel Core and the Inference Spine.
*   **Jetpack Compose:** Modern toolkit for building the native Android UI.

## 3. Native Build & Integration
*   **CMake:** Cross-platform build system for managing C++ compilation.
*   **JNI (Java Native Interface):** Facilitates high-speed communication between the Kotlin bridge and the C++ reasoning spine.

## 4. Data Management & Serialization
*   **SQLite (FTS5):** Enables full-text search and semantic indexing for local files.
*   **Google FlatBuffers:** Used for efficient, zero-copy serialization of checkpoint data and capability manifests.

## 5. Artificial Intelligence (AI) Spine
*   **MediaPipe LLM Inference API:** Provides the underlying engine for Gemma 4 model execution.
*   **Qualcomm Snapdragon AI Stack:** Optimization target for hardware-aware inference.