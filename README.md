# Ronin Kernel

Ronin is a mobile AI runtime for Android with a C++20 native kernel, Kotlin/JNI integration, on-device LiteRT-LM inference, local SQLite memory, and advanced hardware/tool capabilities. The project is aimed at sovereign mobile agent behavior: observe device state, classify intent, retrieve memory, plan actions, execute capabilities, record outcomes, and adapt future routing through feedback.

A core feature of the Ronin Kernel is the **Structural Health Monitoring (SHM) Subsystem**. It includes a real-time native DSP engine with Modal Validation (v3.0) and an AI Review Pipeline that evaluates structural integrity using both local (Gemma) and cloud (Gemini/OpenRouter) AI models.

The repository contains both the Android application and the native runtime it loads. The current implementation is best read as a native cognitive kernel plus an Android shell that provides UI, permissions, hardware access, dynamic cloud provider configuration, and an isolated inference worker process.

This project is currently developed from Termux/Codex CLI on Android. Full host and APK validation is expected to run in GitHub Actions.

## Current Architecture

At runtime the Android app initializes the native kernel through `NativeEngine`, then binds an `InferenceService` running in the `:inference_core` process. User input flows from Kotlin into JNI, through the native intent and graph layers, and either dispatches a deterministic skill or asks the LiteRT-LM reasoning spine for planning or response generation.

Key runtime pieces:

- Native kernel: `ronin_core`, built from `src/` and exposed to Android through `src/ronin_jni.cpp`.
- Android bridge: `NativeEngine.kt`, which owns JNI calls, capability callbacks, cloud inference, model loading coordination, and service binding.
- Inference worker: `InferenceService.kt`, a foreground Android service in `:inference_core` using `com.google.ai.edge.litertlm:litertlm-android:0.12.0`.
- Persistence: SQLite with FTS5 for notes, facts, vault records, episodes, predictions, failures, chat history, graph state, and file indexes.
- Capability execution: C++ skill dispatch plus Kotlin drivers for Android-only actions such as location, SMS, contacts, calendar, files, sensors, and UI-mediated human confirmation.
- Learning and recovery: graph execution, Thompson sampling, failure telemetry, checkpoints, adaptive budgets, speculative execution, and runtime healing controllers.

## Source Map

Native source is organized around the kernel subsystems:

| Path | Purpose |
| --- | --- |
| `src/ronin_kernel.cpp`, `include/ronin_kernel.hpp` | Top-level observe/orient/decide/act kernel loop and world-state hooks. |
| `src/ronin_jni.cpp`, `src/jni_utils.cpp`, `src/jni_gateway.cpp` | JNI registration, Kotlin bridge methods, execution governance, cancellation, memory APIs, sensor APIs, and model notifications. |
| `src/intent_engine.cpp`, `include/intent_engine.h` | Intent classification, slash commands, task planning, skill registry, cloud/offline metadata, and skill execution. |
| `src/agent_scheduler.cpp`, `src/agent_session.cpp`, `include/agent_scheduler.h` | Priority-based background execution of multi-step agent sessions. |
| `src/graph_executor.cpp`, `src/capability_graph.cpp`, `src/graph_storage.cpp` | Capability graph planning, Thompson-sampling route selection, graph persistence, and episode/prediction recording. |
| `src/long_term_memory.cpp`, `src/memory_manager.cpp` | SQLite-backed notes, facts, vault entries, episodes, predictions, files, failures, chat history, memory pressure, and maintenance. |
| `src/capabilities/` | Native skill implementations and Android hardware bridge nodes. |
| `src/reasoning/`, `include/reasoning/` | Active Inference Kernel (RAIK), Free Energy minimization, and Expected Free Energy (EFE) tactical policy router. |
| `src/models/` | Native inference abstraction and model hydration support used by the planner/bridge. |
| `src/dsp/vibe_monitor.cpp`, `src/dsp/resonance_analyzer.cpp` | Real-time sensor DSP analysis, Modal Validation Engine v3, detrending, filtering, and telemetry export. |
| `android/app/src/main/kotlin/com/ronin/kernel/shm/` | SHM Session data models, Android Compose UI bindings, engineering exports, and the AI Review Pipeline. |
| `android/app/src/main/kotlin/com/ronin/kernel/*` | Document Intelligence, ML Kit OCR Engine, Privacy Shield redaction, Privacy Vault, and Jetpack Compose Developer HUD. |
| `src/*healing*`, `src/*budget*`, `src/*checkpoint*`, `src/failure_telemetry_bus.cpp` | Governance, checkpointing, retry, telemetry, and recovery infrastructure. |
| `include/` | Public/native headers mirroring the source modules. |

Android source is under `android/app/src/main/`:

| Path | Purpose |
| --- | --- |
| `kotlin/com/ronin/kernel/MainActivity.kt` | Compose UI, live FEP telemetry gauges, attachment dialogs, and app-level interaction surface. |
| `kotlin/com/ronin/kernel/NativeEngine.kt` | Main Android facade for JNI, AIDL service binding, cloud requests, memory/search APIs, and capability callbacks. |
| `kotlin/com/ronin/kernel/InferenceService.kt` | Isolated LiteRT-LM worker service with streaming AIDL callbacks, model hydration, RAM guard, KV-cache reset, and conversation summarization. |
| `aidl/com/ronin/kernel/` | `IInferenceService` and `IInferenceCallback` contracts for cross-process inference calls. |
| `kotlin/com/ronin/kernel/*Driver.kt` | Android capability drivers for location, SMS, sensors, security, and file/search integration. |
| `assets/capabilities.json`, `assets/providers.json`, `assets/myanmar_dictionary.txt` | Packaged capability/provider config and Myanmar segmentation dictionary. |

Tests live in `tests/` for native host checks and `android/app/src/test/` for JVM-side Android tests.

## Implemented Capabilities

The codebase currently includes:

- On-device Gemma/LiteRT-LM model loading from `.litertlm` files through the isolated inference service.
- Streaming model output over AIDL back to the UI-facing `NativeEngine`.
- Cloud fallback/provider requests through OkHttp using provider configuration JSON.
- Tiered long-term memory APIs for notes, facts, vault data, episodes, predictions, failures, files, and chat history.
- SQLite FTS5 lexical search with Myanmar segmentation support.
- Agent planning and multi-step session scheduling with human-in-the-loop gates for sensitive actions.
- Device world-state injection for battery, RAM, GPS, network, charging, and time-of-day context.
- **Production-Grade Structural Health Monitoring (SHM)**: Ingests 100Hz MEMS accelerometer batches, applies high-pass filtering/detrending, and extracts modal frequencies (F₀) via a robust Modal Validation Engine to achieve <2% F₀ CV repeatability.
- **SHM AI Review Pipeline**: Structures extracted telemetry into engineering JSON, formats it for human consumption, and routes it to cloud (Gemini, OpenRouter) or local (Gemma 4 E2B) LLMs to generate actionable structural reviews, warnings, and confidence scoring.
- **Ronin Active Inference Kernel (RAIK)**: Karl Friston's Free Energy Principle running in a sub-microsecond C++20 hot-loop ($0.81\ \mu\text{s}$) with clamped precision regularization, 3-state adaptive sensory gaze gating (10Hz / 50Hz / 200Hz), and Level 2 Expected Free Energy router with epistemic intent entropy ($H_{\text{slot}}$).
- **Personal Document AI Assistant & OCR**: System photo picker (`PickVisualMedia`) and SAF (`OpenDocument`) via 📎 clip attachment button, interactive `FileResultCard` with FileProvider opening, app/email sharing, on-device Gemma 4 sliding-window summarizer, and Google ML Kit bilingual text recognition.
- **Privacy Shield & Data Redaction**: Real-time regex and token-based on-device redaction for Myanmar NRC, phone numbers, email addresses, and bank cards, with AES-GCM-256 encrypted sandbox vault.
- Runtime health features such as cancellation, safe mode hooks, memory pressure handling, checkpoint storage, telemetry, speculative graph execution, and self-healing controllers.

Legacy design documents and intermediate roadmaps are archived under `docs/archive/` and `old_logs_and_context/`. Consult `docs/USER_GUIDE.md` for user setup and feature usage, `docs/ARCHITECTURE_CURRENT.md` for runtime design, `docs/TECHNICAL_SPECS.md` for engineering specifications, and `docs/MANUAL_INDEX.md` for the complete documentation index.

## Build Requirements

Host/native development:

- CMake 3.22.1 or newer
- C++20 compiler
- Network access during first CMake configure to fetch SQLite amalgamation, FlatBuffers, nlohmann/json, and GoogleTest

Android development:

- Android Gradle Plugin 8.4.2
- Kotlin 2.3.21
- JDK 17
- Android SDK 34
- NDK/CMake integration
- arm64-v8a device or emulator for the Android native build

The Android app depends on LiteRT-LM 0.12.0 and builds `libronin_kernel.so` from the repository root `CMakeLists.txt`.

Local Termux development does not need to run every build command before each commit. Use the commands below when the required tools are available locally; otherwise rely on `.github/workflows/build.yml`.

## Build And Test

Configure and build native host targets:

```bash
cmake -S . -B build_host -DCMAKE_BUILD_TYPE=Debug
cmake --build build_host
```

Run registered native tests:

```bash
cd build_host
ctest --output-on-failure
```

Run the main native test binaries directly:

```bash
cd build_host
./ronin_atomic_test
./ronin_integration_test
./ronin_governance_test
./ronin_self_healing_test
./ronin_evolution_test
./ronin_segmenter_test
```

Build the Android debug APK:

```bash
cd android
gradle assembleDebug
```

Run Android/JVM tests:

```bash
cd android
gradle test
```

## Runtime Setup & User Guide

For step-by-step installation, on-device `.litertlm` setup, cloud API keys, SHM vibration monitoring, hardware tool usage, disaster recovery backup/restore, and troubleshooting, consult the bilingual manual:

- [`docs/USER_GUIDE.md`](file:///data/data/com.termux/files/home/play-ground/ronin/docs/USER_GUIDE.md): Complete End-User & Developer Operations Manual (Myanmar & English).

Cloud inference requires provider configuration (Gemini, OpenAI, OpenRouter) managed securely through the Android app settings. Do not commit API keys, model files, logs, or device-specific secrets.

Useful logcat filters:

```bash
adb logcat -s RoninKernel_Native:V RoninKernel_Worker:V RoninKernel_JNI:V
```

Useful in-app slash commands include `/help`, `/capabilities`, `/status`, `/reset`, `/clear`, `/export`, and `/audit`.

## Technical Documentation

Master index and navigation:
- [`docs/MANUAL_INDEX.md`](file:///data/data/com.termux/files/home/play-ground/ronin/docs/MANUAL_INDEX.md): Complete repository documentation index and cross-references.

Current source-of-truth documents:
- [`docs/USER_GUIDE.md`](file:///data/data/com.termux/files/home/play-ground/ronin/docs/USER_GUIDE.md): Operations manual, setup, hardware control, disaster recovery, and troubleshooting.
- [`docs/ARCHITECTURE_CURRENT.md`](file:///data/data/com.termux/files/home/play-ground/ronin/docs/ARCHITECTURE_CURRENT.md): Production architecture (v5.0+), `KernelRuntimeContext`, IPC daemon, SHM pipeline, governance, and recovery.
- [`docs/TECHNICAL_SPECS.md`](file:///data/data/com.termux/files/home/play-ground/ronin/docs/TECHNICAL_SPECS.md): Current native, Android, inference, persistence, capability, and CI specs.
- [`docs/MANIFEST.md`](file:///data/data/com.termux/files/home/play-ground/ronin/docs/MANIFEST.md): Repository goals, technical stack, module mapping, and architectural rules.
- [`docs/SHM_PIPELINE_V3_ARCHITECTURE.md`](file:///data/data/com.termux/files/home/play-ground/ronin/docs/SHM_PIPELINE_V3_ARCHITECTURE.md): Modal Validation Engine, continuous zero-copy IMU pipeline, and AI Review Layer.

Historical drafts and legacy plans:
- [`docs/archive/`](file:///data/data/com.termux/files/home/play-ground/ronin/docs/archive): Archived blueprints (`BLUEPRINT_V1_3.md`, `MEMORY_MODEL_V2.md`, `SENSOR_DSP_V1.md`, `EVOLUTION_V1_6.md`, `COGNITIVE_RUNTIME_V1_0_PRODUCTION.md`, `COGNITIVE_RUNTIME_V3-V7_PLAN.md`, `BETA_TESTING.md`, etc.). See [`docs/archive/README.md`](file:///data/data/com.termux/files/home/play-ground/ronin/docs/archive/README.md) for historical notes.
- `old_logs_and_context/`: Legacy pre-v2 notes and transcripts.

## Development Notes

- Keep native module structure mirrored between `src/` and `include/`.
- Register new native sources in `CMakeLists.txt`.
- Keep JNI-facing code in `src/ronin_jni.cpp`, `src/jni_utils.cpp`, `src/jni_gateway.cpp`, and the matching Kotlin facade.
- Keep packaged capability manifests synchronized when behavior changes: `assets/capabilities.json` and `android/app/src/main/assets/capabilities.json`.
- Add host tests under `tests/` and register them in `CMakeLists.txt`.
- Add Android/JVM tests under `android/app/src/test/kotlin/`.

## License

This project is licensed under the [MIT License](file:///data/data/com.termux/files/home/play-ground/ronin/LICENSE).
