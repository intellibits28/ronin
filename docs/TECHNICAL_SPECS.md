# Current Technical Specifications

This document describes the current implementation. Legacy specs that described a single-process direct-JNI inference model are archived under `old_logs_and_context/`.

## Runtime Architecture

Ronin currently uses a hybrid native/Android runtime:

- C++20 native kernel built as `ronin_core`.
- Android JNI shared library built as `libronin_kernel.so`.
- Kotlin `NativeEngine` as the app-facing JNI facade.
- Kotlin `InferenceService` in process `:inference_core`.
- AIDL contracts for inference calls and token streaming.
- SQLite databases under the app files directory.

The active inference design is **isolated service + AIDL streaming**, not single-process direct inference callbacks.

## Native Core

The native core is compiled from `RONIN_CORE_SOURCES` in `CMakeLists.txt`.

Major subsystems:

- `RoninKernel`: top-level observe/orient/decide/act loop and world-state state hooks.
- `ActiveInferenceCore`: hot-loop Level 1 Variational Free Energy ($F$) calculation (<1 µs latency) with dimensionless Mahalanobis prediction error and clamped precision regularization.
- `ExpectedFreeEnergyRouter`: Level 2 event-driven Expected Free Energy ($G(\pi)$) policy router (~3 µs latency) over $K=6$ candidate policies with Epistemic Slot Entropy ($H_{\text{slot}}$).
- `IntentEngine`: command handling, intent classification, planner wiring, skill registry, and skill execution.
- `LongTermMemory`: SQLite schema creation, memory APIs, FTS5 search, chat history, failure records, and file indexing.
- `GraphExecutor`: capability graph execution, Thompson-sampling outcome updates, episode and prediction recording.
- `AgentScheduler`: priority queue for multi-step agent sessions.
- `HardwareBridge`: C++ to Kotlin callback surface for Android-only capabilities and inference.
- `ResonanceAnalyzer` & `VibeMonitorEngine`: native DSP summary generation, Welch PSD, Kalman NIS filtering, and 3-state Active Gaze sensory gating (10Hz / 50Hz / 200Hz).
- Execution governance: `JniExecutionGateway`, checkpoints, failure telemetry, adaptive budgets, speculative execution, and runtime healing.

## Android Layer

Key Android classes:

- `MainActivity.kt`: Compose UI, Developer HUD with live FEP telemetry, attachment pickers, and high-level interaction surface.
- `NativeEngine.kt`: JNI facade, service binding, cloud inference, capability callbacks, memory APIs, and native lifecycle.
- `InferenceService.kt`: foreground service running in `:inference_core`; owns LiteRT-LM engine/conversation lifecycle.
- `OcrEngine.kt`: On-device text recognition utilizing Google ML Kit with bilingual Myanmar Unicode and Latin support.
- `DocumentIntelligence.kt`: Plain text, markdown, and PDF document reader with sliding-window chunker and in-document search.
- `PrivacyShield.kt`: Real-time on-device regex and token PII redactor (Myanmar NRC, phone, email, bank accounts).
- `PrivacyVault.kt`: AES-GCM-256 encrypted sandbox storage for private documents.
- `SecurityProvider.kt`: Android-side secret encryption/decryption provider.
- `LocationDriver.kt`, `SmsDriver.kt`, `SensorDriver.kt`, `FileSearchNodeHooks.kt`: Android capability implementations, dynamic sampling rate controllers, and FileProvider/ShareSheet adapters.

The app manifest declares `InferenceService` with:

```xml
android:process=":inference_core"
```

## Inference

Current local inference path:

1. User selects/imports a `.litertlm` model.
2. `NativeEngine.loadModel(path)` waits for `InferenceService`.
3. `InferenceService.loadModel(path)` creates LiteRT-LM `EngineConfig`.
4. Current source sets `maxNumTokens = 1536`.
5. `NativeEngine.notifyModelLoadedNative(path)` notifies native state after successful worker hydration.
6. Native planner or chat skill calls back into Kotlin through `runNeuralReasoning`.
7. `NativeEngine` streams over AIDL and forwards token fragments to UI flow.

The service includes RAM-pressure guards, conversation reset, model auto-rehydration after unstable failures, and summarization/reset support.

## Persistence

Native persistence is SQLite-backed. CMake fetches SQLite amalgamation and compiles it with `SQLITE_ENABLE_FTS5=1`.

Current native memory database schema is defined in `LongTermMemory::initSchema()` and includes:

- `notes` and `notes_fts`
- `facts`
- `vault`
- `episodes` and `episodes_fts`
- `document_embeddings` and `document_chunks_fts` (External-content FTS5 with `ai`/`ad`/`au` sync triggers and `UNIQUE(doc_path, model_id, chunk_index)` multi-model coexistence)
- `predictions`
- `chat_history`
- `file_index`
- `audit`
- `failures`

Schema changes are managed through the native `schema_version` table (currently v3). Current migrations archive legacy tables safely, maintain compound indexes (`idx_doc_path_model`), rebuild FTS content, and keep notes, episodes, and document chunks FTS rows synchronized on insert, update, and delete.

## Capabilities And Safety

Capabilities are split between:

- Native skills in `src/capabilities/`.
- Centralized policy governance in `CapabilityPolicyEngine` (`src/capability_policy_engine.cpp`) reading `assets/capabilities.json`.
- Android drivers/callbacks in Kotlin (`LocalResearchEngine.kt`, `McpClientManager.kt`, `DocumentIntelligence.kt`, `OcrEngine.kt`).
- Capability graph nodes persisted through `GraphStorage`.
- Human-in-the-loop (HITL) confirmation for sensitive planner actions, MCP write tools, SMS, and calendar operations.

## Build And CI

The project is developed on Termux, with full validation running in GitHub Actions:

- `.github/workflows/build.yml` (`Ronin Kernel Build & Test`): Host C++ tests (`ctest`), Android debug APK assembly, and GitHub Release publication.
- `.github/workflows/ronin-ci.yml` (`Ronin Isolated Inference CI`): Inference module validation, benchmarks, and JNI shared library verification.

## Completed Architectural & Security Upgrades (v5.1.1)

- Split JNI implementation into modular bindings (`src/ronin_jni.cpp`, `src/jni/jni_memory_bindings.cpp`, `src/jni/jni_registration.cpp`).
- Encapsulated native runtime ownership in RAII-managed `KernelRuntimeContext` (`src/jni/ronin_jni_context.h`).
- Implemented schema v3 SQLite migrations with multi-model vector embedding coexistence and 16-byte aligned SIMD Cosine Similarity + FTS5 Hybrid RRF search.
- Centralized capability policy enforcement via `CapabilityPolicyEngine` and hardened `scripts/termux_mcp_server.py` (eliminated arbitrary shell execution, enforced HMAC-SHA256 + Timestamp + Nonce LRU anti-replay).
- Implemented AES-256-GCM encrypted `.roninvault` WAL-checkpointed snapshot backups via Android SAF.
