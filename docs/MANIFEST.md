# Project Ronin: Mobile AI Kernel Manifest

## Hardened Goal
Build a sovereign, fault-tolerant Mobile AI runtime platform utilizing the **Hardened v1.3 Cognitive Systems Architecture** (10-Layer Design).

## Core Tech Stack
- **Engine:** C++20 (Cognitive Kernel), Kotlin (Android Instrumentation & Security)
- **Inference:** LiteRT-LM 0.12.0 (Gemma 4 Optimized, 1024-2048 Token Range)
- **Security:** AES-256-GCM (Hardware Keystore + Biometric HITL)
- **Database:** SQLite FTS5 (Classified Memory Tiers: NOTE, FACT, VAULT, EPISODE)
- **Reasoning:** Bayesian Thompson Sampling with Symmetric Confidence Decay

## Key Cognitive Components
1. **Memory Classifier:** Deterministic routing of data to tiered storage based on goal intent.
2. **Episodic Logging:** Automated task summarization and FTS5 indexing of past interactions.
3. **Hardware Vault:** Biometric-locked secret store utilizing device-level encryption.
4. **World State Layer:** 1Hz telemetry injection (Battery, RAM, Network) for context awareness.
5. **Prediction Engine:** Expectation vs Reality tracking for internal belief calibration.

## Repository Mapping (v11.3 Alignment)
- `src/ronin_jni.cpp`: JNI bridge for World State, Predictions, and Memory Search.
- `src/long_term_memory.cpp`: Tiered storage implementation (v13.0 Schema).
- `src/intent_engine.cpp`: Task Planner and Memory Classifier logic.
- `src/agent_scheduler.cpp`: Multi-step orchestration and automated episodic logging.
- `android/app/src/main/kotlin/com/ronin/kernel/SecurityProvider.kt`: Hardware encryption provider.

## Engineering Mandates
- **Thermal Safe UI:** Developer HUD must throttle to 1Hz refresh rate.
- **Biometric HITL:** Any access to VAULT or sensitive FACTS must require user authentication.
- **Lexical Priority:** Prefer precise keyword/fact lookup over probabilistic LLM inference for knowledge retrieval.

*Current Build: v10.1.7 (Cognitive Alpha)*
