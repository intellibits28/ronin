🧠 Ronin Kernel - AI Context & Engineering Standards (HARDENED v3.0)

## Project Overview
Modular AI agent runtime, Android (Snapdragon 778G+), C++20 + Kotlin-JNI.
Core Philosophy: A single-spine reasoning kernel, hardened for production reliability.

## Current Phase: 11.2 (Hardened v3.3 Production)

## ⚠️ CRITICAL ARCHITECTURAL RULES (v3.3 PRODUCTION)

### 1. Single Reasoning Spine (Gemma 4 Only)
*   **Engine:** Gemma 4 (LiteRT-LM 0.12.0) is the ONLY reasoning engine. 
*   **Vectorless Persistence:** SQLite FTS5 for fast lexical keyword retrieval.
*   **Direct JNI Bridge:** Native Direct Bridge (Single-Process) with AIDL-fallback for multi-process stability on SD778G+.

### 2. User-Centric Sampling (T,P,K)
*   **Dynamic Controls:** UI must provide sliders for Temperature (0.1-1.5), Top-K (1-100), and Top-P (0.1-1.0).
*   **Thinking Toggle:** User can choose to hide/show `[THINK]` tokens in the UI.

### 3. Streaming & Bubble Integrity
*   **Zero-Empty Bubbles:** Bubbles must show "Ronin is reasoning..." status immediately upon initiation.
*   **Tag Separation:** `[THINK]` tokens route to Logs; `[REPLY]` tokens route to Bubble.

### 3. JNI Memory Safety (The JNI Guard)
*   **Deep-Copy Requirement:** All JNI strings must be immediately deep-copied to `std::string`.
*   **Thread Hygiene:** ScopedJniEnv is mandatory for all background thread callbacks.

### 4. Memory & LMK Guards (SD778G+ Tuning)
*   **Token Limit:** `maxNumTokens` is hard-capped at **512** for SD778G+ prefill stability.
*   **RAM Guard:** Automatic KV-cache pruning (reset) is triggered if free RAM falls below **0.8GB**.

## Memory Persistence: Spec v2.1
* **Core Strategy:** Lexical Keyword Search via FTS5.
* **Storage:** `ronin_cognitive.db` (Chat History) and `ronin_memory.db` (FTS5 Keywords).

## Audit & Verification Protocol
1.  **Thinking Filter:** All model reasoning (`[THINK]`) must be stripped before SQLite persistence to prevent context poisoning.
2.  **Instruction Isolation:** System instructions are sent ONCE at the start of a conversation to optimize KV cache.
3.  **Tool Depth:** `MAX_TOOL_CALL_DEPTH = 1` is strictly enforced.

### 7. Security Posture: Sovereign Control Mode
*   **Sandboxing:** NativeBridge operates in 'Read-only' mode by default. System-wide changes require explicit User Consent.
*   **Atomic Shutdown:** `Kernel::Shutdown()` must munmap native memory and close LiteRT sessions atomically.
*   **Sovereign Override (`FORCE_EXECUTE`):** User can bypass safety filters, but all such actions are recorded in the Audit-trail.
