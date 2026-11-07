# Ronin Project Manifest

This manifest reflects the current repository implementation. Older architecture notes were moved to `old_logs_and_context/` so they remain available without being treated as the active design.

## Goal

Ronin is a mobile AI runtime for Android. It combines a C++20 native cognitive kernel, Kotlin/JNI Android integration, LiteRT-LM on-device inference, SQLite/FTS5 memory, and tool-capability execution for local agent workflows.

The project is developed primarily on-device through Termux and Codex CLI. Full build validation is expected to run in GitHub Actions.

## Current Stack

- **Native core:** C++20, CMake, SQLite FTS5, FlatBuffers, nlohmann/json, PFFFT.
- **Android app:** Kotlin/JVM 17, Jetpack Compose, AIDL, Android SDK 34.
- **Inference:** LiteRT-LM Android SDK 0.12.0 in an isolated foreground service.
- **Bridge:** JNI for native kernel APIs; AIDL for cross-process inference streaming.
- **Memory:** SQLite-backed notes, facts, vault entries, episodes, predictions, failures, chat history, and file index.
- **Capabilities:** Native skills plus Android drivers for hardware and OS integrations.
- **CI/build authority:** GitHub Actions workflows under `.github/workflows/`.

## Runtime Shape

1. `MainActivity` owns the app UI and user interaction.
2. `NativeEngine` loads `libronin_kernel.so`, initializes the native runtime, registers callback hooks, and binds `InferenceService`.
3. `InferenceService` runs in Android process `:inference_core`, hydrates `.litertlm` models, and streams tokens over AIDL.
4. `src/ronin_jni.cpp` routes Kotlin calls into the native runtime.
5. `IntentEngine` classifies input, handles slash commands, and chooses direct skill execution or planner-backed agent execution.
6. `AgentScheduler` and `GraphExecutor` run multi-step sessions, record episodes, report outcomes, and update graph confidence.
7. `LongTermMemory` persists facts, notes, vault data, episodes, predictions, failures, files, audit records, and chat history.

## Authoritative Source Map

- `CMakeLists.txt`: native library, host tests, and third-party native dependencies.
- `src/`, `include/`: C++ kernel, memory, graph, planning, DSP, capability, and governance implementation.
- `android/app/src/main/kotlin/com/ronin/kernel/`: Android UI, JNI facade, inference service, security provider, and drivers.
- `android/app/src/main/aidl/`: inference service callback contracts.
- `assets/`: source capability/provider config copied into Android assets by GitHub Actions.
- `android/app/src/main/assets/`: packaged app assets used by local Android builds.
- `tests/`: host-native tests.
- `android/app/src/test/`: Android/JVM tests.

## Completed Milestones (v5.1.1 Sovereign Tactical Upgrade)

1. Split `src/ronin_jni.cpp` into focused binding modules (`src/jni/jni_memory_bindings.cpp`, `src/jni/jni_registration.cpp`).
2. Replaced native global runtime pointers with an explicit RAII `KernelRuntimeContext`.
3. Added schema-versioned SQLite v3 migrations, external-content FTS5 sync triggers, and Hybrid RRF vector store (`document_embeddings`).
4. Implemented Privacy-Aware Local Research Engine (`/research`) with DuckDuckGo Lite POST, 256 KB stream cap, SSRF DNS-pinning, and Gemma 4 E2B synthesis.
5. Hardened Termux MCP Bridge (`/mcp`) with HMAC-SHA256 + Timestamp + Nonce LRU anti-replay and strict Capability Security Policy.
6. Replaced cleartext DB dumps with AES-256-GCM encrypted `.roninvault` snapshots via Android SAF.

See `RELEASE_NOTES_v5.1.1.md` and `CHANGELOG.md` for full release details.
