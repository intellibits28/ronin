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
- `IntentEngine`: command handling, intent classification, planner wiring, skill registry, and skill execution.
- `LongTermMemory`: SQLite schema creation, memory APIs, FTS5 search, chat history, failure records, and file indexing.
- `GraphExecutor`: capability graph execution, Thompson-sampling outcome updates, episode and prediction recording.
- `AgentScheduler`: priority queue for multi-step agent sessions.
- `HardwareBridge`: C++ to Kotlin callback surface for Android-only capabilities and inference.
- `ResonanceAnalyzer`: native DSP summary generation for batched sensor samples.
- Execution governance: `JniExecutionGateway`, checkpoints, failure telemetry, adaptive budgets, speculative execution, and runtime healing.

## Android Layer

Key Android classes:

- `MainActivity.kt`: Compose UI and high-level interaction surface.
- `NativeEngine.kt`: JNI facade, service binding, cloud inference, capability callbacks, memory APIs, and native lifecycle.
- `InferenceService.kt`: foreground service running in `:inference_core`; owns LiteRT-LM engine/conversation lifecycle.
- `SecurityProvider.kt`: Android-side secret encryption/decryption provider.
- `LocationDriver.kt`, `SmsDriver.kt`, `SensorDriver.kt`, `FileSearchNode.kt`: Android capability implementations and adapters.

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
- `predictions`
- `chat_history`
- `file_index`
- `audit`
- `failures`

Current gap: schema changes are not yet managed through an explicit `schema_version` migration system.

## Capabilities And Safety

Capabilities are split between:

- Native skills in `src/capabilities/`.
- Android drivers/callbacks in Kotlin.
- Capability graph nodes persisted through `GraphStorage`.
- Human-in-the-loop confirmation for sensitive planner actions such as SMS and calendar operations.

Current gap: sensitive capability policy is distributed across JNI, planner logic, manifests, and Android permission handling. It should be centralized in a policy engine.

## Build And CI

The project is developed on Termux, but full validation should run in GitHub Actions.

Primary workflow:

- `.github/workflows/build.yml`

The workflow runs host tests, then builds the Android debug APK and uploads it as an artifact. Local Termux work should prioritize small edits, static review, and focused commands; CI should be treated as the build authority.

## Known Technical Debt

- `src/ronin_jni.cpp` is too broad and should be split into focused binding files.
- Native runtime ownership relies on process-global pointers.
- SQLite migration strategy is ad hoc.
- Several bridge APIs return plain strings for typed failures.
- Capability policy and audit rules are not centralized.
- Structured observability is incomplete for multi-step agent sessions.
