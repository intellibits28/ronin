🧠 Ronin Kernel - AI Context & Engineering Standards (HARDENED v3.6)

## Project Overview
Modular AI agent runtime, Android (Snapdragon 778G+), C++20 + Kotlin-JNI.
Core Philosophy: A single-spine reasoning kernel, hardened for production reliability.

## Current Phase: 11.2 (Hardened v3.6 Production)

## ⚠️ CRITICAL ARCHITECTURAL RULES (v3.6 PRODUCTION)

### 1. Single Reasoning Spine (Gemma 4 Only)
*   **Engine:** Gemma 4 (LiteRT-LM 0.12.0) is the ONLY reasoning engine. 
*   **Real-time Streaming:** Reactive `ChatMessage` properties (MutableState) ensure sub-second token rendering.
*   **Vectorless Persistence:** SQLite FTS5 for fast lexical keyword retrieval.
*   **Direct JNI Bridge:** Native Direct Bridge (Single-Process) with AIDL-fallback for multi-process stability.

### 2. User-Centric Sampling & UI
*   **Dynamic Controls:** UI provides sliders for Temperature (0.1-1.5), Top-K (1-100), and Top-P (0.1-1.0).
*   **Cloud Integration:** Multi-step provider setup (Gemini, OpenAI, OpenRouter) with dynamic model fetching and simplified API key entry.
*   **Thinking Toggle:** User can choose to hide/show `[THINK]` tokens in the UI.

### 3. Streaming & Bubble Integrity
*   **Zero-Empty Bubbles:** Bubbles show "Ronin is reasoning..." status immediately.
*   **Atomic Updates:** Reactive state-driven updates prevent message display hangs.
*   **Tag Separation:** `[THINK]` tokens route to Logs; `[REPLY]` tokens route to Bubble.

### 4. JNI Memory & Thread Safety
*   **Deep-Copy Requirement:** All JNI strings must be immediately deep-copied to `std::string`.
*   **Thread Hygiene:** ScopedJniEnv mandatory for callbacks. Network calls MUST use `Dispatchers.IO` to avoid `NetworkOnMainThreadException`.

### 5. Memory & LMK Guards (SD778G+ Tuning)
*   **Token Limit:** `maxNumTokens` is hard-capped at **512** for SD778G+ stability.
*   **RAM Guard:** Automatic KV-cache pruning triggered if free RAM falls below **0.8GB**.
*   **Atomic Persistence:** `CheckpointEngine` uses `memfd_create` (Android) or `mkstemp` (Host) for crash-safe shadow buffering.

## Memory Persistence: Spec v2.1
* **Core Strategy:** Lexical Keyword Search via FTS5.
* **Storage:** `ronin_cognitive.db` (Chat History) and `ronin_memory.db` (FTS5 Keywords).

## Audit & Verification Protocol
1.  **Thinking Filter:** All model reasoning (`[THINK]`) stripped before SQLite persistence.
2.  **Instruction Isolation:** System instructions sent ONCE at conversation start.
3.  **Tool Depth:** `MAX_TOOL_CALL_DEPTH = 1` strictly enforced.

### 7. Security Posture: Sovereign Control Mode
*   **Sandboxing:** NativeBridge operates in 'Read-only' mode by default.
*   **Atomic Shutdown:** `Kernel::Shutdown()` munmaps native memory and closes sessions atomically.
*   **Sovereign Override (`FORCE_EXECUTE`):** User bypass recorded in Audit-trail.
