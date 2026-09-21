# Current Architecture

This is the source-of-truth architecture document for the active Ronin implementation.

## Process Model

Ronin uses two Android processes:

- **Main app process (`com.ronin.kernel`)**: Hosts the Compose UI, `NativeEngine`, JNI runtime initialization, Cloud Providers (Gemini, OpenAI, OpenRouter, Custom), and Android capability drivers.
- **Inference worker process (`:inference_core`)**: Foreground `InferenceService` hosting LiteRT-LM (Gemma 4 Production SDK) engine, KV cache, and conversation state.

Inference streaming crosses the process boundary through high-performance AIDL IPC:

- `IInferenceService.aidl`: Loads models and triggers asynchronous token streaming.
- `IInferenceCallback.aidl`: Streams partial token fragments back to the UI in real-time.

A custom Gradle compilation task (`generateTermuxAidlStubs`) synthesizes these AIDL interfaces natively, allowing full compilation on AArch64 / Termux as well as desktop CI.

## Request Flow

1. User sends input via `MainActivity` Compose UI.
2. `NativeEngine.processInputAsync()` assigns a unique `sessionId`, `execId`, and `corrId`.
3. `processInputNative()` enters `JniExecutionGateway` which enforces governance and policy checks.
4. Slash command handling intercepts built-in commands (`/help`, `/capabilities`, `/status`, `/skills`, `/model`, `/reset`, `/reflect`).
5. `IntentEngine.process()` classifies the user request into an action or conversational reply.
6. If the request requires multi-step planning, `TaskPlanner` generates an execution plan.
7. Sensitive actions (e.g. Vault access, SMS dispatch) trigger **Human-In-The-Loop (HITL)** biometric/confirmation dialogs.
8. `AgentScheduler` executes approved multi-step capability sequences.
9. `GraphExecutor` traverses capability nodes and executes functional or Android drivers.
10. `LongTermMemory` persists cognitive notes, facts, episodes, audit logs, and updates SQLite FTS5 search indexes.

## Inference Architecture (Hybrid Local + Cloud)

Ronin employs an intelligent hybrid inference architecture:

1. **On-Device Core (`InferenceService`)**: Powered by LiteRT-LM with Gemma 4 `.litertlm` models. Uses quantized KV cache, memory pressure guards (0.8GB RAM threshold), and reflection caching for rapid token streaming.
2. **Cloud Escalation**: When on-device inference is unavailable or Cloud-Only mode is enabled, `HardwareBridge` dispatches requests to user-configured cloud endpoints (Gemini, OpenAI, OpenRouter, Custom) with real-time latency measurement.

## State And Ownership (`KernelRuntimeContext`)

Native runtime state is encapsulated within an RAII-managed `KernelRuntimeContext`:

- `instance`: Global JNI reference to the calling engine.
- `ltm`: `std::shared_ptr<LongTermMemory>` managing SQLite FTS5 and episodic storage.
- `graph_storage` & `cap_graph`: `GraphStorage` and `CapabilityGraph` instances.
- `graph_executor`: `GraphExecutor` orchestrating node traversal.
- `memory_manager`: `MemoryManager` handling working context limits.
- `resonance_analyzer`: `ResonanceAnalyzer` native vibration DSP engine.
- `intent_engine`: `IntentEngine` coordinating intent classification, planning, and macro-skills.

When the Android service shuts down or re-initializes, `KernelRuntimeContext::release()` safely cleans up all pointers, unregisters telemetry buses, and prevents dangling pointer / Use-After-Free (UAF) crashes.

## Structural Health Monitoring (SHM) Pipeline v3

Ronin integrates an industrial-grade vibration analysis and structural resonance detection pipeline:

- **Sensors**: 100Hz 3-axis accelerometer streaming via `VibeMonitorEngine`.
- **DSP Core**: Zero-padded 2048-pt Welch Fast Fourier Transform (FFT) utilizing `pffft` with 512-sample sub-windows (<0.05Hz modal resolution).
- **Modal Validation Engine v3**: Evaluates peak prominence, Signal-to-Noise Ratio (SNR), Q-factor, and structural priors across X/Y/Z axes.
- **Kalman Filter with NIS Gating**: Tracks resonant frequencies with adaptive tolerance hysteresis, rejecting transient shock noise.
- **Export & Privacy**: Serializes diagnostic sessions to engineering JSON, human-readable text reports, or privacy-masked summaries with token limits for AI review.

## Security & Governance Model

- **Human-in-the-Loop (HITL)**: High-risk operations (Vault access, device modifications) strictly require user consent.
- **Encrypted Vault**: Sensitive facts and credentials stored in AES-256-GCM / EncryptedSharedPreferences with biometric authentication. Unconditional auth bypasses are prohibited.
- **Android Backup Disabled**: `android:allowBackup="false"` prevents extraction of private database files across unencrypted ADB backups.
- **FTS5 Query Sanitization**: User input is sanitized and bound with `SQLITE_TRANSIENT` to prevent SQL syntax injection errors.

## Persistence & Disaster Recovery

- **Databases**:
  - `ronin_cognitive.db`: SQLite database storing memories, facts, perception states, and FTS5 indexes.
  - `ronin_graph.db`: Capability graph topology.
  - `checkpoint.bin`: Shadow buffer checkpointing for crash-consistent state recovery.
- **In-App Disaster Recovery**: Settings includes one-tap SQLite database backup to `/sdcard/Download/ronin_cognitive_backup.db` and Storage Access Framework (SAF) export/import.
- **Fresh Install Auto-Recovery**: On startup, an empty database automatically checks for and restores previous backups in Downloads.
- **Deterministic APK Signing**: The repository includes a permanent `debug.keystore`, ensuring all GitHub Actions artifact APKs share the exact same SHA-256 signature, allowing seamless in-place updates without losing local data.
