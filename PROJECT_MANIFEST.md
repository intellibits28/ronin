# Project Ronin: Mobile AI Kernel Manifest

## Goal
Build a shippable, fault-tolerant Mobile AI runtime kernel optimized for Android (SD778G focus).

## Core Tech Stack
- **Language:** C++20 (Core Engine), Kotlin (Android Wrapper & OS Hooks)
- **Serialization:** FlatBuffers (Zero-copy)
- **Inference:** NEON SIMD, Qualcomm AI Stack (NPU)
- **Build System:** CMake, GitHub Actions (CI/CD)

## Key Components to Implement
1. **LiteRT 0.11.0 Migration:** Mandatory upgrade for Mixture of Experts (MoE) support and optimized Gemma 4 inference.
2. **Adaptive Checkpoint Engine:** LMK-aware survival using shadow buffers (C++20).
3. **Capability Graph:** Dynamic routing with Thompson Sampling (O(1) LUT) (C++20).
4. **Memory Manager:** Tri-anchor KV-cache pruning (C++20).
5. **JNI Bridge:** Zero-copy DirectByteBuffer mapping (Kotlin/C++20).

## CI/CD Strategy
- **Compiler:** Android NDK (Clang)
- **Testing:** GoogleTest (GTest)
- **Build:** Automated via GitHub Actions on every push.
