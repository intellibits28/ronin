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

## Ronin Active Inference Kernel (RAIK)

Rooted in Karl Friston's Free Energy Principle (FEP), Ronin implements a hierarchical, dual-speed Active Inference architecture for autonomous homeostasis and adaptive sensory regulation:

- **Level 1: Fast Sensory Microkernel (10–100 Hz, C++20)**:
  - Instantaneous, dimensionless Mahalanobis prediction error: $\tilde{\varepsilon}_i = (o_i - g(s_i)) / \sigma_{i,\text{baseline}}$.
  - Decoupled block-diagonal precision regularization: $\Pi_i = \operatorname{clamp}(1/(\sigma_i^2 + 10^{-6}), 10^{-3}, 10^{3})$.
  - Scalar Variational Free Energy: $F = \frac{1}{2}\sum_{i=1}^4 (\Pi_i \tilde{\varepsilon}_i^2 - \ln \Pi_i)$.
  - Zero dynamic heap allocations in hot-loop; measured mean latency is **$0.81\ \mu\text{s}$** (< 1 µs).
  - **Adaptive Sensory Gaze Controller**: Dynamically switches accelerometer sampling between `QUIESCENT` (10Hz, ~1.2 mA at $F < 0.8$), `VIGILANT` (50Hz, ~4.5 mA at $0.8 \le F < 2.5$), and `ACTIVE_INVESTIGATION` (200Hz, ~14.0 mA at $F \ge 2.5$).
- **Level 2: Tactical Intent & Policy Engine (Event-Driven, C++20)**:
  - Single-step lookahead ($T=1$) Expected Free Energy (EFE) evaluator across base policies (`IDLE`, `SAMPLE_HIRES`, `CLARIFY_USER`, `INSPECT_DOC`, `EXEC_TOOL`, `SELF_HEAL`) and extended subsystem primitives (`LEXICAL_ONLY = 6`, `RESEARCH_HOP = 7`, `ESCALATE_CLOUD = 8`, `MCP_HITL_CONFIRM = 9`).
  - Balances pragmatic utility $(\mathbb{E}[o \mid \pi] - C)^2$ against epistemic information gain $\ln(\sigma_{\text{prior}}^2 / \sigma_{\text{posterior}}^2)$. Measured evaluation latency is **$3.08\ \mu\text{s}$**.
  - **Epistemic Slot Entropy ($H_{\text{slot}}$)**: Computes intent entropy $H_{\text{slot}} = 0.4(1 - \text{conf}) + 0.6(N_{\text{missing}} / N_{\text{total}})$. When $H_{\text{slot}} > 0.45$, `CLARIFY_USER` is selected to proactively request missing parameters before execution.
- **4-Phase Unified Subsystem Expansion**:
  1. **Phase 1 — Dual-Gate BM25 Entropy for Smart RAG Wakeup (`/docsearch`)**: Evaluates temperature-scaled Softmax probabilities $p_i = \frac{\exp(|s_i|/\tau)}{\sum_j \exp(|s_j|/\tau)}$ and normalized Shannon entropy $\hat{H} = -\frac{1}{\ln K}\sum_{i=1}^K p_i \ln p_i \in [0, 1]$ over top-$K$ ($K=5$) SQLite FTS5 BM25 hits (`ExpectedFreeEnergyRouter::evaluateRagWakeup` & `LongTermMemory::computeLexicalFreeEnergy`). Selects `LEXICAL_ONLY` (skipping ~165 MB `EmbeddingGemma 2` model swap) iff $|s_1| > \theta_{\text{min}}$ AND $\hat{H} < 0.35$; otherwise selects `INSPECT_DOC`.
  2. **Phase 2 — Privacy-Weighted Brain Router & Multi-Hop Research (`/research`)**:
     - `evaluateBrainRoute`: Combines pre-inference query complexity $H_{\text{prior}}$ and 8-token speculative probe entropy $H_{\text{probe}}$ while injecting a $+100.0$ pragmatic cost when `PrivacyShield` detects PII (`pii_risk_flag > 0`), mathematically guaranteeing local execution over `ESCALATE_CLOUD`.
     - `evaluateResearchHop`: Governs multi-hop web foraging using a quadratic hop cost $C_{\text{hop}}(k) = c_0 \cdot k^2$ ($k_{\max}=2$) and diminishing-returns early-stopping ($\Delta F = F_{k-1} - F_k < \epsilon$).
  3. **Phase 3 — Sliding-Window MCP Anomaly Gate & Surprise-Driven Reflection (`/mcp` & `/reflect`)**:
     - `CapabilityPolicyEngine::evaluateMcpToolCall`: 32-slot C++ ring buffer tracking short-window burst rate surprise ($\lambda_t$ vs EWMA interval), path sensitivity (`..`, `.ssh`, `.env`, `/sdcard/dcim`, `ronin_cognitive`), and intent-path divergence. Escalates `ALLOW` tools (`read_file`, `list_files`) to `DEFER` (`MCP_HITL_CONFIRM`) when Contextual Surprise $F_{\text{mcp}} > 0.70$.
     - `LongTermMemory::storeFact`: Computes write-time epistemic `surprise_score` against existing `(entity, attribute)` beliefs. During `/reflect` (`native_runNightlyReflection`), if device temperature $< 38^\circ\text{C}$, reconciles the top-5 highest-surprise facts (`getTopSurprisingFacts(5)`).
  4. **Phase 4 — Lock-Free Audio Kalman Precision Filter**: `ResonanceAnalyzer::computeAudioKalmanPrecision` executes inline on the real-time audio/DSP thread without mutex locks or heap allocations, dynamically weighting sensory gain $K_t = \frac{\Pi_{\text{audio}}}{\Pi_{\text{audio}} + \Pi_{\text{prior}}}$ via $\Pi_{\text{audio}} = \exp(-\beta \cdot \text{noise\_rms})$.
- **Developer HUD Telemetry**: Real-time JNI export of Free Energy $F$, Gaze State, and selected Policy to Compose HUD gauges.

## Document Intelligence & Hybrid RRF RAG (`/docsearch`)

Ronin features an integrated on-device personal document intelligence and Hybrid RAG subsystem bridging Android OS pickers, native SQLite vector search, and local LLMs:

- **Attachment Gateway (📎)**:
  - **Photo Picker (`PickVisualMedia`)**: Privacy-first system photo picker that reads images without requiring `READ_MEDIA_IMAGES` permissions.
  - **Document Picker (`OpenDocument`)**: SAF integration enabling universal browsing across device storage, SD cards, and cloud drives.
- **Interactive `FileResultCard`**:
  - Dispatches `FileProvider` intents for viewing and `ACTION_SEND` ShareSheet intents for email/app sharing.
  - Features one-tap clipboard path copy and on-device Gemma 4 sliding-window document summarization.
  - Native Google ML Kit OCR engine extracting bilingual Myanmar Unicode and English text.
- **Hybrid RRF Vector Store (`document_embeddings`)**:
  - Supports 3 model tiers (`EmbeddingGemma-2-270M`, `440M`, `740M`) with multi-model coexistence (`UNIQUE(doc_path, model_id, chunk_index)`).
  - External-content SQLite FTS5 synchronization triggers (`document_embeddings_ai`, `document_embeddings_ad`, `document_embeddings_au`).
  - 16-byte aligned SIMD Cosine Similarity combined with SQLite FTS5 BM25 via Reciprocal Rank Fusion ($k=60$).
  - Mutex-guarded 16-chunk Binder IPC sub-batching and clean fallback to `MyanmarSegmenter` + FTS5 BM25 when no embedding model is loaded.
- **Command Dispatch**:
  - `/files <query>`: Rapid FTS5 lexical file index search.
  - `/summarize <path>`: Direct chunked summarization.
  - `/docsearch <path> <query>`: Hybrid RRF semantic + lexical in-document chunk retrieval.

## Privacy-Aware Local Research Engine (`/research`)

- **Zero Cloud LLM Tracking**: Queries DuckDuckGo Lite via HTTP `POST` (`application/x-www-form-urlencoded`) to prevent query string and referrer leakage.
- **Resource & SSRF Hardening**: Enforces a 256 KB decompressed stream cap and strict DNS-pinning across redirect hops (blocking loopback, RFC1918 private, link-local, CGNAT, multicast, and broadcast IPs).
- **100% On-Device Synthesis**: Synthesizes extracted content locally using Gemma 4 E2B with inline `[1][2][3]` citations.
- **Provenance Persistence ("SAVE TO VAULT")**: Never auto-injects unverified web results into LTM; requires explicit user confirmation to save with `source_type = "web_research"` provenance metadata.

## Hardened Termux MCP Bridge (`/mcp`)

- **Loopback Cryptographic Authentication**: Connects to `http://127.0.0.1:8765/mcp` without cleartext `Bearer` tokens, signing every request with HMAC-SHA256 (`X-Ronin-Signature`), `X-Ronin-Timestamp` (`±30s` window), and a 16-byte random hex nonce (`X-Ronin-Nonce` + LRU replay cache).
- **Capability Security Policy**:
  - `ALLOW`: Safe read-only tools (`get_device_info`, `list_files`, `read_file` with 128 KB cap, `git_status`, `git_diff`).
  - `CONFIRM`: Modifying tools gated behind Human-In-The-Loop (HITL) approval.
  - `DENY`: Arbitrary shell execution (`run_bash`, `shell`, `exec`) permanently blocked (CWE-78 remediation).

## Privacy Shield & Data Redaction

- **Local PII Redaction Engine**: Real-time on-device regex and token filter safeguarding sensitive Myanmar personal data:
  - Myanmar National Registration Card numbers (NRC) $\to$ `[NRC_REDACTED]`.
  - Phone numbers $\to$ `[PHONE_REDACTED]`.
  - Email addresses $\to$ `[EMAIL_REDACTED]`.
  - Bank and payment card numbers $\to$ `[ACCOUNT_REDACTED]`.
- **Pre-Flight Sanitization**: Automatically intercepts extracted OCR text and outgoing cloud prompts when Privacy Shield is toggled active.
- **Encrypted Privacy Vault**: File sandbox using AES-GCM-256 for secure document storage.

## Security & Governance Model

- **Human-in-the-Loop (HITL)**: High-risk operations (Vault access, device modifications) strictly require user consent.
- **Encrypted Vault**: Sensitive facts and credentials stored in AES-256-GCM / EncryptedSharedPreferences with biometric authentication. Unconditional auth bypasses are prohibited.
- **Scoped FileProvider & Path Sanitization**: File imports sanitize `DISPLAY_NAME` and verify canonical directory containment (`CWE-22`); `file_paths.xml` strictly scopes sharing to `documents/`, `attachments/`, and `docs/` (`CWE-732`).
- **Android Backup Disabled**: `android:allowBackup="false"` prevents extraction of private database files across unencrypted ADB backups.
- **FTS5 Query Sanitization**: User input is sanitized and bound with `SQLITE_TRANSIENT` to prevent SQL syntax injection errors.

## Persistence & Encrypted Disaster Recovery (`.roninvault`)

- **Databases**:
  - `ronin_cognitive.db`: SQLite database storing memories, facts, vector embeddings (`document_embeddings`), perception states, and FTS5 indexes.
  - `ronin_graph.db`: Capability graph topology.
  - `checkpoint.bin`: Shadow buffer checkpointing for crash-consistent state recovery.
- **Encrypted Cognitive Vault Backup (`.roninvault`)**: Eliminates cleartext database exports (`CWE-312`). Executes `PRAGMA wal_checkpoint(TRUNCATE)` for atomic consistency, encrypts the snapshot via `PrivacyVault` (AES-256-GCM), and exports/imports through Android Storage Access Framework (SAF).
- **Deterministic APK Signing**: The repository includes a permanent `debug.keystore`, ensuring all GitHub Actions artifact APKs share the exact same SHA-256 signature, allowing seamless in-place updates without losing local data.
