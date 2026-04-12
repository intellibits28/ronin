# Ronin: Mobile AI Kernel

A shippable, fault-tolerant Mobile AI runtime kernel optimized for Android (Snapdragon 778G focus), implementing high-efficiency inference and survival mechanisms.

## Core Architectural Pillars

### 1. Binary Checkpoint Schema (FlatBuffers)
To achieve zero-copy state restoration, Ronin utilizes a specialized **FlatBuffers** schema. This allows the kernel to map checkpoint data directly from disk/memory into C++ structures without deserialization overhead.
*   **NEON Optimization:** Fields are 16-byte aligned to ensure direct SIMD access.
*   **Shadow Buffers:** Dual-buffer system for atomic state updates, preventing corruption during sudden process termination.
*   **Intent Tracking:** Thompson Sampling-based routing keys are embedded to resume execution at the optimal capability node.

### 2. Tri-Anchor KV Model
Memory is the primary bottleneck on mobile. The Tri-Anchor model manages the KV-cache through a three-point pruning strategy:
*   **Recency Anchor:** Preserves the immediate local context for conversational fluidity.
*   **Saliency Anchor:** Protects high-attention tokens identified during the forward pass.
*   **Semantic Anchor:** Retains compressed global context tokens to prevent "context drift" during long-running sessions.

### 3. Adaptive LMK Heuristics
The kernel is "LMK-Aware," designed to survive Android's Low Memory Killer (LMK) by monitoring system pressure signals.
*   **Proactive Eviction:** Triggering KV-cache pruning *before* the system reaches critical memory thresholds.
*   **O(1) Checkpointing:** Rapid serialization of the execution frontier when a high-pressure signal is detected.
*   **DirectByteBuffer Mapping:** Minimizing the heap footprint by moving large tensors and caches into native memory, making the process less likely to be targeted by the LMK.

## Development & CI/CD
*   **Language:** C++20 / Zig (Engine Core), Kotlin (JNI Wrapper).
*   **Build System:** CMake + Android NDK.
*   **Verification:** GitHub Actions (Ubuntu-latest) running NDK-cross-compilation and GoogleTest (GTest) suites on every push.
