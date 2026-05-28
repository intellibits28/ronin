🧠 Ronin Kernel - AI Context & Engineering Standards (HARDENED v4.0)

## Project Overview
Modular AI agent runtime, Android (Snapdragon 778G+ / Mid-range), C++20 + Kotlin-JNI.
Core Philosophy: Sovereign edge intelligence with multi-process isolation and linguistic precision.

## Current Phase: 11.2 (Hardened v4.0 Production)

## ⚠️ CRITICAL ARCHITECTURAL RULES (v4.0 PRODUCTION)

### 1. Dual-Process Reasoning Spine
*   **Process Isolation:** Inference runs in `:inference_core` to protect UI stability on mid-range devices.
*   **Engine:** Gemma 4 (LiteRT-LM 0.12.0) is the primary local reasoning engine.
*   **Real-time Streaming:** Reactive `ChatMessage` properties (MutableState) ensure zero-lag token rendering.
*   **AIDL Bridge:** Secure cross-process token streaming with `@Keep` reflection safety.

### 2. Linguistic Precision (Myanmar Text)
*   **Token Limit:** `maxNumTokens` defaults to **1024** (Optimal for UTF-8 Multi-byte).
*   **Extended Range:** UI slider allows up to **2048** tokens for deep context.
*   **Trie-based BWS:** Pure C++ Trie Segmenter (23k+ words) for precise lexical keyword extraction.

### 3. Reliable Cloud Connectivity
*   **OkHttp Stack:** Modern network stack replacing legacy HttpURLConnection.
*   **Smart Setup:** Pre-filled profiles for Gemini, OpenAI, and OpenRouter (API Key focus).
*   **Dynamic Fetching:** Real-time model discovery via provider APIs.

### 4. Memory & LMK Defense
*   **Atomic Persistence:** `CheckpointEngine` uses `memfd_create` (Android) or `mkstemp` (Host) for crash-safe shadow buffering.
*   **RAM Guard:** Automatic KV-cache pruning if free RAM falls below **1.1GB** (Mid-range tuned).

### 5. Audit & Verification Protocol
1.  **Thinking Filter:** All model reasoning (`[THINK]`) stripped before SQLite persistence.
2.  **Instruction Isolation:** System instructions sent ONCE at conversation start.
3.  **Tool Depth:** `MAX_TOOL_CALL_DEPTH = 1` strictly enforced.

## Memory Persistence: Spec v2.1 (Hardened)
* **Strategy:** Lexical Keyword Search via FTS5 with Trie-segmented Myanmar text.
* **Storage:** `ronin_cognitive.db` (Chat) and `ronin_memory.db` (Keywords).

### 7. Security Posture: Sovereign Control Mode
*   **Sandboxing:** NativeBridge operates in 'Read-only' mode by default.
*   **Atomic Shutdown:** `Kernel::Shutdown()` munmaps native memory and releases JNI resources.
*   **Encrypted Secrets:** API keys stored via `EncryptedSharedPreferences`.
