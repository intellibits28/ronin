🧠 Ronin Kernel - AI Context & Engineering Standards (HARDENED v3.0)

## Project Overview
Modular AI agent runtime, Android (Snapdragon 778G+), C++20 + Kotlin-JNI.
Core Philosophy: A single-spine reasoning kernel, hardened for production reliability.

## Current Phase: 11.2 (Hardened v3.0 Production)

## ⚠️ CRITICAL ARCHITECTURAL RULES (v3.0 PRODUCTION)

### 1. Single Reasoning Spine (Gemma 4 Only)
*   **Engine:** Gemma 4 (LiteRT-LM 0.12.0) is the ONLY reasoning engine. 
*   **Vectorless Persistence:** All legacy embedding models (BGE/E5) have been REMOVED. Ronin relies purely on SQLite FTS5 for fast lexical keyword retrieval and Gemma 4's In-context Reasoning for semantic depth.
*   **Direct JNI Bridge:** The AIDL/IPC transport for tokens has been replaced with a **Native Direct Bridge** (Single-Process). Token fragments are streamed via direct JNI callbacks (`pushTokenToUI`) for SHM-like performance.

### 2. Precise Intent Routing (Lexical Spine)
*   **Matching:** Hardware intents (GPS, Flashlight, etc.) must use **Strict Token-based matching** via `IntentEngine.cpp`. 
*   **Avoid Substrings:** Fuzzy substring matching is FORBIDDEN for hardware triggers to prevent general questions from misrouting.

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
