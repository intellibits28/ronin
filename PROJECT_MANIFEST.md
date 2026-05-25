# Project Ronin: Mobile AI Kernel Manifest

## Hardened Goal
Build a sovereign, fault-tolerant Mobile AI runtime kernel utilizing the **Hardened v3.0 Production Architecture** (SD778G focus).

## Core Tech Stack
- **Engine:** C++20 (Kernel Core), Kotlin (Android UI & JNI Wrapper)
- **Inference:** LiteRT-LM 0.12.0 (Gemma 4 Optimized)
- **Bridge:** Native Direct Bridge (Direct JNI Callbacks)
- **Database:** SQLite FTS5 (Lexical Keyword Spine)
- **Build System:** CMake, Android NDK 26b, GitHub Actions (JDK 21)

## Key Hardened Components
1. **Direct JNI Bridge:** Single-process token streaming replacing legacy AIDL/SHM.
2. **Lexical Intent Spine:** Precise token-based hardware routing (IntentEngine.cpp).
3. **Thinking Filter:** Regex-based reasoning pruning for clean SQLite persistence.
4. **RAM Guard:** Real-time LMK-aware KV-cache pruning (0.8GB threshold).
5. **Instruction Isolation:** Single-injection system prompt for context efficiency.

## Repository Mapping (v3.0 Alignment)
- `src/ronin_jni.cpp`: Production JNI method table and callback bridge.
- `android/app/src/main/aidl/`: High-level service definitions.
- `include/chat_template_formatter.h`: Gemma 4 turn formatting logic.
- `src/intent_engine.cpp`: Lexical keyword matching spine.
- `src/long_term_memory.cpp`: Persistent SQLite history with thinking filter.

## Engineering Mandates
- **Single Process:** InferenceService and MainActivity must reside in the same process.
- **Synchronous Logic:** Native reasoning calls must block for the kernel result while streaming fragments to UI.
- **Deep Copy:** All JNI strings must be deep-copied before usage in C++.

*Current Build: v4.7.26.05.24*
