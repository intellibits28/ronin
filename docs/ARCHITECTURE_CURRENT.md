# Current Architecture

This is the source-of-truth architecture document for the current implementation.

## Process Model

Ronin uses two Android processes:

- Main app process: UI, `NativeEngine`, JNI runtime initialization, cloud provider requests, Android capability coordination.
- `:inference_core`: foreground `InferenceService` process that owns LiteRT-LM engine and conversation state.

Inference streaming crosses the process boundary through AIDL:

- `IInferenceService`
- `IInferenceCallback`

This replaces older single-process direct inference assumptions.

## Request Flow

1. User sends input in `MainActivity`.
2. `NativeEngine.processInputAsync()` creates `sessionId`, `execId`, and `corrId`.
3. `processInputNative()` enters `JniExecutionGateway`.
4. Native command handling runs first for slash commands.
5. `IntentEngine.process()` classifies the request.
6. If the request is a planner task, native code asks the inference spine to create a plan.
7. Sensitive plans request human confirmation where required.
8. `AgentScheduler` schedules approved multi-step sessions.
9. `GraphExecutor` dispatches capabilities and records outcomes.
10. `LongTermMemory` persists messages, facts, episodes, predictions, failures, and search indexes.

## Inference Flow

1. `NativeEngine.initialize()` binds `InferenceService`.
2. `NativeEngine.loadModel(path)` calls `IInferenceService.loadModel(path)`.
3. `InferenceService` creates a LiteRT-LM `Engine` and `Conversation`.
4. Native planner/chat calls Kotlin `runNeuralReasoning(input)`.
5. `NativeEngine` calls `IInferenceService.streamReasoning`.
6. AIDL callback emits tokens to `NativeEngine.pushTokenToUI`.
7. UI observes `inferenceFlow`.

## State And Ownership

Current native runtime state is process-global:

- `g_kernel`
- `g_intent_engine`
- `g_ltm`
- `g_graph_storage`
- `g_cap_graph`
- `g_graph_executor`
- `g_memory_manager`
- `g_resonance_analyzer`
- `g_llm_context`

This works for the current app but is the main refactor target. The desired direction is a `KernelRuntimeContext` object with explicit init/shutdown/reinit semantics.

## Capability Flow

Native capabilities use:

- `CapabilityGraph`
- `CapabilityDispatcher`
- `BaseSkill` implementations
- `HardwareBridge` for Android callbacks
- Kotlin `ICapabilityDriver` implementations

Sensitive actions currently gate in native/Kotlin paths. The desired direction is one central policy layer that decides permission requirements, human confirmation, audit level, risk level, and whether offline/cloud execution is allowed.

## Persistence Flow

Primary databases:

- `ronin_cognitive.db` for long-term memory and runtime records.
- `ronin_graph.db` for graph persistence.

SQLite FTS5 is enabled in native CMake and used for lexical lookup.

## CI And Development Model

This repository is edited from Termux/Codex CLI on Android. Local work should avoid assuming desktop build tools are installed. GitHub Actions is the expected build/test authority for:

- Host C++ tests.
- Android APK build.
- Release artifact generation.

When changing implementation, prefer small commits that CI can validate independently.
