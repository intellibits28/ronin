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

## Capability Flow & Sensor Runtime v2.0

Native capabilities and sensor integrations are managed dynamically:

- **`ToolRegistry`**: A central dynamic string-based tool registry storing metadata (inputs/outputs, permissions, description) and implementations for both class-based `BaseSkill`s and functional tools (e.g. C++ DSP wrappers like FFT, butterworth filters, peak detection, zero crossing, and RMS).
- **`CapabilityDiscoveryEngine`**: Resolves requirements semantically using Jaccard index similarity on tool descriptions, and automatically constructs a Directed Acyclic Graph (DAG) using a greedy topological sorting scheduler matching inputs and outputs.
- **`PerceptionEngine`**: A 10Hz rule-based background thread in Kotlin that fuses accelerometer/DSP signals to classify user context (e.g., `walking`, `running`, `phone_on_table`, `phone_in_pocket`, `building_vibration`), saving updates to the SQLite `perception_history` table and syncing them with the C++ `BeliefState` working memory.
- **`SkillCompiler`**: A self-learning compiler that scans successful SQLite episodes and promotes recurrent sequences (e.g. `audio_capture` -> `fft`) into virtual compound Macro-Skills registered dynamically in the `ToolRegistry`.
- **`CapabilityDispatcher` & `HardwareBridge`**: Orchestrate traditional JNI/Android hardware hooks.
- **Kotlin `ICapabilityDriver`**: Bridges Kotlin/Java capability calls.

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
