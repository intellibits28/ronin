# Technical Specifications: Mobile AI Cognitive Kernel (Ronin) v3.0

## 1. Architecture Overview: Hardened Unified Infrastructure
The "Ronin" v3.0 architecture implements a hardened, single-process autonomous AI assistant infrastructure. The core cognitive engine—including the Lexical Intent Spine, Memory Manager, and Native Direct Bridge—is implemented in **C++20**. The Android UI, JNI Direct Callbacks, and LiteRT-LM reasoning spine are implemented in **Kotlin/JNI**.

Key Components:
 * **Kernel Core (C++20):** Manages high-level orchestration, intent routing, and long-term memory.
 * **Lexical Intent Spine:** Strict token-based hardware routing using SQLite FTS5 for zero-latency capability selection.
 * **Native Direct Bridge:** Eliminates AIDL/IPC overhead by utilizing direct JNI callbacks from the Inference Spine to the UI flow within a single process.
 * **Neural Spine (LiteRT-LM):** High-performance Gemma 4 inference optimized for Snapdragon 778G+ with prefill stability hardening.
 * **RAM Guard:** Real-time LMK-aware KV-cache pruning triggered at 0.8GB free RAM threshold.

## 2. Logic & Algorithms
### Lexical Intent Matching
The core hardware matching is performed via exact token comparison.
 * **Logic:** Input is tokenized and compared against a defined `capabilities.json` registry.
 * **Hardening:** Requires both a **Subject** and an **Action** match to trigger hardware nodes (e.g., "Location" + "Show"), preventing general reasoning from misrouting.

### Context Management (Hardening v3.0)
 * **Instruction Isolation:** System prompts are injected ONCE at the start of a conversation to maximize KV cache efficiency.
 * **Thinking Filter:** Model reasoning tokens (`[THINK]`) are parsed in the UI for display but stripped via regex before SQLite persistence to prevent context poisoning.
 * **Token Capping:** `maxNumTokens` is hard-set to **512** to align with SD778G+ prefill buffer limits.

## 3. Data Structures
### Lexical Persistence (SQLite FTS5)
 * **FTS5 Virtual Tables:** Used for high-speed keyword retrieval in `ronin_memory.db`.
 * **Chat History:** Cleaned (thinking-free) turns stored in `ronin_cognitive.db` for turn-based context reconstruction.

### JNI Direct Callback Table
Designed for zero-lag token streaming:
 * **pushTokenToUI:** Direct JNI jump from background inference thread to UI SharedFlow.
 * **runNeuralReasoning:** Synchronous kernel-to-UI bridge for multi-step tool calls.

## 4. Platform Constraints
### Mobile Hardware targeting
 * **Architecture:** Snapdragon 778G / Cortex-A78.
 * **Memory Guard:** Dynamic KV-cache reset triggered if `OS_FREE_RAM < 800MB`.
 * **Thermal Guard:** Scaling to scalar paths or cloud fallback if `TEMP > 42°C`.

### Operating System (Android)
 * **Single Process Mandate:** InferenceService must share the Main process to ensure JNI Direct Bridge reliability.
 * **Storage:** Mandatory filename resolution and compiled-cache purging to prevent storage bloat (>1GB).

## 5. Implementation Roadmap (Hardened v3.0)
### JNI Direct Bridge (src/ronin_jni.cpp)
 1. Implement `native_pushTokenToKernel` for direct token ingestion.
 2. Ensure thread-safe GlobalRef for `MainActivity` instance.
 3. Use `ScopedJniEnv` for all worker-to-UI callbacks.

### Lexical Intent Spine (src/intent_engine.cpp)
 1. Transition from fuzzy substring search to exact token-set comparison.
 2. Implement dual-condition Subject+Action check.
 3. Default to `ChatSkill (ID 1)` for all non-hardware queries.

### RAM Guard & Stability (InferenceService.kt)
 1. Set `maxNumTokens = 512` in `EngineConfig`.
 2. Monitor `getAvailableRAM()` before every inference.
 3. Execute `resetConversation()` if RAM pressure is critical.
