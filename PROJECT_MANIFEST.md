# Project Ronin: Mobile AI Kernel Manifest

## Hardened Goal
Build a sovereign, fault-tolerant Mobile AI runtime kernel utilizing the **Hardened v3.6 Production Architecture** (SD778G focus).

## Core Tech Stack
- **Engine:** C++20 (Kernel Core), Kotlin (Android UI & JNI Wrapper)
- **Inference:** LiteRT-LM 0.12.0 (Gemma 4 Optimized)
- **Bridge:** Native Direct Bridge (Direct JNI Callbacks)
- **Database:** SQLite FTS5 (Lexical Keyword Spine)
- **Build System:** CMake, Android NDK 26b, GitHub Actions (JDK 21)

## Key Hardened Components
1. **Direct JNI Bridge:** Single-process token streaming with reactive UI updates.
2. **Real-time Streaming:** Reactive `ChatMessage` properties for sub-second rendering.
3. **Cloud Profile Setup:** Simplified multi-step setup with dynamic model fetching.
4. **RAM Guard:** Real-time LMK-aware KV-cache pruning (0.8GB threshold).
5. **Instruction Isolation:** Single-injection system prompt for context efficiency.

## Repository Mapping (v3.6 Alignment)
- `src/ronin_jni.cpp`: Production JNI method table and callback bridge.
- `android/app/src/main/kotlin/com/ronin/kernel/MainActivity.kt`: Hardened reactive UI.
- `src/checkpoint_engine.cpp`: Atomic persistence with host-side fallback.
- `src/intent_engine.cpp`: Lexical keyword matching spine.
- `src/long_term_memory.cpp`: Persistent SQLite history with thinking filter.

## Engineering Mandates
- **Single Process:** InferenceService and MainActivity must reside in the same process.
- **Synchronous Logic:** Native reasoning calls must block for the kernel result while streaming fragments to UI.
- **Deep Copy:** All JNI strings must be deep-copied before usage in C++.

*Current Build: v4.7.26.05.24*
