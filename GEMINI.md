# Ronin Kernel - AI Context & Engineering Standards

## Project Overview
Modular AI agent runtime, Android (Snapdragon 778G+), C++20 + Kotlin-JNI.

## Current Phase
Phase 4.0: Vtable-based Skill Registry (Unified Interface).

## ⚠️ CRITICAL RULES (MUST FOLLOW)
1. **Unified Architecture:** Both cognitive skills (e.g., Embedding) and hardware tools (e.g., GPS, WiFi) MUST inherit from the same `BaseSkill` interface. No split routing pipelines.
2. **JNI Thread Safety:** C++ execution threads MUST NEVER block waiting for Android callbacks. All hardware calls must be strictly asynchronous. Always verify `AttachCurrentThread`/`DetachCurrentThread` pairing and JNI MethodID routing.
3. **Language Strictness:** Never suggest Java. Use Kotlin exclusively for the JVM side.
4. **Memory & LMK Guards:** Check GlobalRef lifecycles. Ensure the C++ Reasoning Spine respects Android's Low Memory Killer (LMK) by not busy-waiting.
