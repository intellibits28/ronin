# Project Ronin: Mobile AI Kernel Manifest

## Goal
Build a shippable, fault-tolerant Mobile AI runtime kernel optimized for Android (SD778G focus).

## Core Tech Stack
- **Language:** C++20 / Zig (Engine), Kotlin (Android Wrapper)
- **Serialization:** FlatBuffers (Zero-copy)
- **Inference:** NEON SIMD, Qualcomm AI Stack (NPU)
- **Build System:** CMake, GitHub Actions (CI/CD)

## Key Components to Implement
1. **Adaptive Checkpoint Engine:** LMK-aware survival using shadow buffers.
2. **Capability Graph:** Dynamic routing with Thompson Sampling (O(1) LUT).
3. **Memory Manager:** Tri-anchor KV-cache pruning.
4. **JNI Bridge:** Zero-copy DirectByteBuffer mapping.

## CI/CD Strategy
- **Compiler:** Android NDK (Clang)
- **Testing:** GoogleTest (GTest)
- **Build:** Automated via GitHub Actions on every push.
