# Changelog

All notable changes to the Ronin Kernel project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [5.1.3] - 2026-11-11

### Added
- **Research-Grade SHM DSP Architecture**:
  - **64-bit IEEE 754 Precision (`double`) Filter Pipeline**: Migrated `HighPassBiquad` filter coefficients ($b_0, b_1, b_2, a_1, a_2$) and state delay registers ($z_1, z_2$) from 32-bit `float` to 64-bit `double` precision, eliminating quantization limit cycles, truncation noise, and pole instability near the unit circle ($z \approx 0.9968 \pm j0.0022$) for ultra-low frequency structural monitoring down to $0.05\text{ Hz}$.
  - **Median + 3*MAD Non-Stationary Impulse Segment Gating**: 2-pass Welch sub-window segment rejection detecting and skipping localized impulse noise (footsteps, door slams, dropped objects) without corrupting the ambient vibration noise floor or aborting resonance analysis.
  - **Nakamura Horizontal-to-Vertical (H/V) Spectral Ratio Validation**: Native evaluation of directional spectral energy ratio ($\text{H/V} = \frac{S_{xx} + S_{yy}}{2 \cdot S_{zz}}$) for all candidate peaks, granting $+10\%$ score bonus for validated horizontal superstructure sway ($\text{H/V} \ge 2.0$) and applying $-15\%$ penalty for localized floor slab bounce ($\text{H/V} < 0.8$ with $Z$-dominance).
  - **4-Factor Research-Grade Quality Gate (`ShmQualityReport`)**: Composite 100-point physical quality scoring fusing Sensor Stability ($30\%$), Noise Margin ($25\%$), Coupling Stillness Quality ($25\%$), and Frequency Stability ($20\%$), classifying measurements into `RESEARCH_GRADE` ($\ge 85$), `GOOD` ($\ge 70$), `FAIR` ($\ge 50$), and `UNRELIABLE` ($< 50$).
  - **Stationary Coupling Stillness via EMA Gravity Variance**: Low-pass Exponential Moving Average ($\alpha = 0.02$) gravity vector tracking to verify physical stillness and surface contact ($\sigma_g^2 \le 0.002$) vs handheld motion/tremor.
  - **Anomaly-First Baseline Protection Gate**: Automatic lockout gate preventing degraded or damaged structural readings ($|\Delta f_0\%| > 5.0\%$ or $z > 2.0\sigma$) from contaminating healthy historical statistics, with incremental online Welford running mean and standard deviation updates for healthy readings.
  - **Eurocode 8 / ASCE 7 Empirical Building Code Prior**: Dynamic building fundamental frequency prior ($f_{\text{expected}} = \frac{10.0\text{ Hz}}{N_{\text{floors}}}$) configured via command JSON (`building_floors` / `floors`).
  - **Android UI & Export Integration**: Live Quality Gate badge rendering in `ShmUiComponents.kt`, H/V sway metrics in `ShmSessionDetailDialog`, and full JSON serialization/deserialization across `ShmSession.kt`, `MainActivity.kt`, and developer debug logs.
  - **Host Regression Test Suite**: Added 5 dedicated validation tests in `tests/shm_pipeline_test.cpp` verifying impulse gating, H/V sway discrimination, quality scoring, anomaly protection, and building floor priors (14/14 tests passing).

## [5.1.2] - 2026-11-11

### Added
- **Multi-Centroid Semantic Intent Router**: Cascaded intent routing across 12 primary intents (`VAULT_LOOKUP`, `VAULT_UPDATE`, `VAULT_SAVE`, `LOCATION`, `DEVICE_SETTINGS`, `FILE_SEARCH`, `SEND_SMS`, `SET_ALARM`, `SHM_VIBRATION`, `PITCH_ANALYSIS`, `CHAT_QUERY`, `FLASHLIGHT`) partitioned into 3 language registers: `*_EN` (English), `*_MM` (Burmese), and `*_MIX` (Burm-glish) with score max-pooling.
- **Strict L2 Re-Normalization**: Pre-computed centroid vectors and vectorized query embeddings strictly unit-normalized ($\|\vec{C}_k\|_2 = 1.0$), ensuring exact cosine similarity dot products in $[-1, 1]$.
- **Top-2 Margin Ambiguity Guard & Lexer Tie-Breaker**: Margin enforcement ($S_1 \ge \tau$ and $S_1 - S_2 \ge 0.04$) with deterministic C++ lexical tie-breaker checking mutation verbs (`"ပြောင်း"`, `"ပြင်"`, `"update"`, `"change"`) vs creation/read markers (`"သိမ်း"`, `"မှတ်"`, `"ပြ"`, `"save"`, `"show"`), safely routing unresolved ambiguity to `IntentCategory::CLARIFICATION`.
- **Stateful Multi-Turn Context Carry-Over**: Non-destructive slot carry-over (`PendingSlotState`, `ttl_turns = 1`) enabling interactive clarification when vault update parameters are missing, automatically fulfilling the update on follow-up user input.
- **Universal Value-Based Credential Sanitizer**: Real-time regex pattern scanner in C++ (`CredentialSanitizer`) and Kotlin masking Google API keys (`AIza...`), OpenAI keys (`sk-...`), GitHub PATs (`ghp_...`), Bearer headers, and Burmese assignment markers (`<secret> အဖြစ် / လို့ / သို့`) in-place as `[REDACTED_SECRET]` across logs, DevHUD, telemetry, and UI.
- **Comprehensive Test Coverage**: Registered `ronin_semantic_routing_test` host C++ test suite (15 tests) and `SemanticRoutingSafetyTest` Android JVM test suite (7 tests) verifying unit norms, max-pooling, tie-breaking, secret redaction, and multi-turn carry-over.

### Changed
- **Intent Engine JNI & Routing Binding**: Connected `SemanticRouter` centroid matcher and `PendingSlotState` into `IntentEngine::process` and `TaskPlanner::tryFastPathRoute`.
- **HardwareBridge & FailureTelemetry Redaction**: Integrated `CredentialSanitizer::sanitizeText` across `HardwareBridge::pushMessage`, `HardwareBridge::updateDevHUD`, and `FailureTelemetryBus::logFailure`.

### Fixed
- **Vault In-Place Update & Secret Query Routing**: Fixed false location trigger on secret queries (`"show my gemini api key"`), correctly routing to `VAULT_LOOKUP`, and enabled in-place updates for existing SQLite vault rows.
- **Burmese Agglutinative Syntax Parsing**: Prevented multibyte UTF-8 byte offset corruption in Burmese slot slicing by using ASCII-lowercased string references.

## [5.1.1] - 2026-11-07

### Added
- **Tactical Brutalist UI & Myanmar Typography**: Unified AMOLED pure black theme (`#0A0A0A`), `8.dp` rounded corner geometry across all UI cards, diacritic-safe Myanmar dual-font system (`21.sp` body with `PlatformTextStyle(includeFontPadding = true)`), and dismissible slash command overlay with header `✕`, back button, and tap-outside handling.
- **On-Demand EmbeddingGemma 2 & Hybrid RAG (`/docsearch`)**: Configurable 3 model tiers (`270M`, `440M`, `740M`), multi-model SQLite embedding coexistence under `UNIQUE(doc_path, model_id, chunk_index)`, 16-byte aligned SIMD Cosine Similarity, and automatic fallback to `MyanmarSegmenter` + SQLite FTS5 BM25 lexical search when no model is active.
- **Privacy-Aware Local Research Engine (`/research`)**: 100% on-device synthesis with Gemma 4 E2B, DuckDuckGo Lite form-encoded search, 256 KB stream limit, SSRF redirect DNS-pinning, and interactive "SAVE TO VAULT" persistence with citation provenance.
- **Hardened Termux MCP Bridge (`/mcp`)**: Authenticated local bridge with HMAC-SHA256 request signing, `±30s` timestamp window, 16-byte random hex nonce LRU cache, and strict Capability Security Policy (`ALLOW`, `CONFIRM`, and `DENY` for arbitrary shell execution).
- **Encrypted Cognitive Vault Backup & Restore (`.roninvault`)**: Atomic WAL-checkpointed SQLite snapshotting encrypted with AES-256-GCM via Android Storage Access Framework (SAF).
- **4-Phase Active Inference Subsystem Expansion**:
  - **Phase 1 (Smart RAG Wakeup)**: Dual-Gate BM25 normalized Shannon entropy evaluator (`LEXICAL_ONLY` vs `INSPECT_DOC`) to skip unnecessary `EmbeddingGemma 2` model swaps on exact lexical matches.
  - **Phase 2 (Privacy-Weighted Brain Router & Multi-Hop Research)**: $+100.0$ PII pragmatic cost barrier against `ESCALATE_CLOUD` and quadratic hop cost $C_{\text{hop}}(k) = c_0 \cdot k^2$ ($k_{\max}=2$) with $\Delta F < \epsilon$ early-stopping for `/research`.
  - **Phase 3 (MCP Anomaly Gate & Surprise-Driven LTM Reflection)**: 32-slot ring buffer escalating `ALLOW` tools to `MCP_HITL_CONFIRM` when contextual surprise $F_{\text{mcp}} > 0.70$, plus write-time `surprise_score` tagging in `facts` and thermal-safe ($< 38^\circ\text{C}$) top-5 reconciliation during `/reflect`.
  - **Phase 4 (Lock-Free Audio Kalman Precision Filter)**: Real-time inline precision-weighted Kalman gain ($\Pi_{\text{audio}} = \exp(-\beta \cdot \text{noise\_rms})$) in `ResonanceAnalyzer`.

### Changed
- **Database Schema v3 Migration**: Added compound index `idx_doc_path_model` and automatic SQLite FTS5 external-content triggers (`document_embeddings_ai`, `document_embeddings_ad`, `document_embeddings_au`) to synchronize `document_chunks_fts` on inserts, deletes, and updates.
- **Binder IPC Sub-Batching**: Mutex-guarded 16-chunk sub-batching across Android Service and C++ Kernel to eliminate `TransactionTooLargeException`.
- **JNI Architecture Hardening**: Standardized `LongTermMemory` namespace qualification across all JNI bindings.

### Fixed
- **CodeQL #18 (CWE-78)**: Eliminated arbitrary shell command execution in `scripts/termux_mcp_server.py`.
- **CodeQL #7 (CWE-190)**: Fixed 32-bit multiplication before 64-bit conversion in `pffft.c:1161`.
- **Security Audit (RONIN-SEC-01 to RONIN-SEC-08)**:
  - Sanitized display names and validated canonical paths during model and traineddata imports (CWE-22).
  - Scoped `file_paths.xml` by removing root and external storage access (CWE-732).
  - Removed cleartext `Authorization: Bearer` token over loopback HTTP (CWE-319).
  - Eliminated cleartext cognitive database export to public storage (CWE-312).
  - Aligned capability manifest paths with `capabilities.json` (CWE-1188).
  - Added thread-safe mutex guards to model registry and cloud providers (CWE-362).
  - Migrated legacy `PreferenceManager` APIs to secure preferences (CWE-676).
  - Enforced memory dimension bounds (`dim <= 1024`) and byte alignment validation (CWE-400).

## [3.0.0] - 2026-07-28

### Added
- **Production-Grade SHM AI Review Pipeline**: Export SHM analysis metrics in structured formats (Engineering JSON, Human Report, Developer Debug Logs).
- **SHM Compose Canvas Chart**: Lightweight, custom Canvas visualizer for drawing Structural Health Monitoring frequency peaks directly in the Android UI.
- **AI Review Integrations**: Added support for structural AI reviews using Local Gemma (via Edge inference) or Cloud Gemini/OpenRouter via the `ShmAiReviewPipeline`.
- **Clipboard Copy Support**: Included one-click clipboard copy features for Executive Summaries and AI Reports inside the SHM diagnostic views.
- **Cloud Provider Management UI**: Revamped `SettingsSection` and `ModelPicker` allowing developers to set custom endpoints and input API keys for OpenRouter, OpenAI, and Custom endpoints.
- **Industrial Modal Validation Engine v3**: Implemented Native C++ `vibe_monitor.cpp` updates featuring a sub-window Welch PSD, axis coherence checking, and multi-stage confidence scoring for SHM data.
- **Outlier Gate Hysteresis**: Added state-machine based hysteresis in the Native Decision Engine to effectively manage and stabilize `baseline_f0` shifts over time.
- **SHM Stress Evaluation Harness**: Synthetic data generators for quantitative benchmark reporting of SHM engine accuracy.
- **Architecture Documentation**: Documented the full data flow and system capabilities of the new pipeline inside `docs/SHM_PIPELINE_V3_ARCHITECTURE.md`.

### Changed
- **Modular Android UI Architecture**: Refactored the monolithic `MainActivity.kt` into `RoninUIComponents.kt`, cleanly separating the `ReasoningConsole`, `DeveloperHud`, and `SystemStatusCard`.
- **Dynamic F0 UI Binding**: The `ShmDetailScreen` now dynamically binds `baseline_f0`, `filtered_f0`, and `vibration_energy` directly from live C++ telemetry rather than static mocks.
- **AI LaTeX Parsing**: Extracted outputs from the Local Gemma 4 E2B model are now aggressively parsed (e.g., stripping `$\text{}$` blocks) to render cleanly without external Markdown libraries.
- **Native Telemetry Logs**: C++ Decision Engine trace logs (Layer 1, Layer 2, Modal Validation steps) are passed up cleanly via JNI into the Compose UI in real-time.

### Fixed
- Fixed critical brace cut-offs and UI compilation syntax errors during the UI modularization phase.
- Fixed Cloud provider dynamic model listing returning generic 404 errors by implementing proper `NativeEngine` Endpoint override checking.
- Fixed hardcoded baseline F0 fallback showing `0.1 Hz` constantly in the Android HUD.
- Fixed non-responsive fallback text inside the AI review dialog when local reasoning traces were missing.
