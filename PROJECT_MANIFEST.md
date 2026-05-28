# Project Ronin: Mobile AI Kernel Manifest

## Hardened Goal
Build a sovereign, fault-tolerant Mobile AI runtime kernel utilizing the **Hardened v4.0 Production Architecture** (SD778G / Mid-range focus).

## Core Tech Stack
- **Engine:** C++20 (Kernel Core), Kotlin (Android UI & JNI Wrapper)
- **Inference:** LiteRT-LM 0.12.0 (Gemma 4 Optimized, 1024 Token Default)
- **Bridge:** Native Direct Bridge + AIDL (Dual-Process Isolation)
- **Networking:** OkHttp 4.12.0 (Reliable Cloud Inference)
- **Database:** SQLite FTS5 (Trie-Segmented Myanmar Keywords)

## Key Hardened Components
1. **Dual-Process Isolation:** Inference isolated in `:inference_core` for UI stability.
2. **Real-time Streaming:** Reactive `ChatMessage` properties for zero-lag rendering.
3. **OkHttp Stack:** Modern network implementation with proper timeouts and error handling.
4. **Trie-based BWS:** Pure C++ segmenter with 23k+ Myanmar words for memory precision.
5. **RAM Guard:** Real-time LMK-aware KV-cache pruning (1.1GB threshold).


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
