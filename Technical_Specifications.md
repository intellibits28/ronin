# Technical Specifications: Mobile AI Cognitive Kernel (Ronin)
## 1. Architecture Overview: Mobile Autonomous Assistant Infrastructure
The "Ronin" project aims to build a mobile-focused autonomous AI assistant infrastructure, initially described in the repository as written in **Zig**, but with core performance-critical components implemented in **C++** using **NEON SIMD intrinsics**. The architecture includes several key components:
 * **Native Inference Engine (Kernel):** Implements an optimized C++ core for vector operations (NEON SIMD).
 * **Survival Core (Checkpoint Engine):** Logic for LMK (Low Memory Killer) survival using shadowed buffers and SQLite L3 deep-store.
 * **Tri-Anchor KV Model:** Resolves mobile memory limitations via resolution-degradation memory zones.
 * **Reasoning Spine (Capability Graph):** Logic for dynamic capability selection and tool routing using Thompson sampling.
 * **JNI Bridge & Android Wrapper:** Connects the native kernel to a Jetpack Compose UI with zero-copy access to memory buffers.
## 2. Logic & Algorithms
### Intent Similarity Kernel (NEON SIMD)
The core semantic matching is performed by a vectorized function, compute_intent_similarity_neon.
 * **Primary Logic:** Calculates cosine similarity with pre-normalized vectors. Range: [-1.0, 1.0].
 * **Thermal-Aware Fallback:** Monitors a global g_thermal_state. If state is SEVERE, it branches to a scalar implementation (compute_similarity_scalar) to mitigate thermal throttling.
 * **Normalization scaling:** 127 \times 127 = 16129 for integer arithmetic during dot product.
### Capability Graph Routing
 * **Routing Algorithm:** Uses an **Alias Method** for discrete approximation of **Thompson Sampling** to select capability nodes.
 * **Mathematical Audit (CI/CD):** Builds are validated by executing 10,000 samples against a known Beta distribution (\alpha=10, \beta=5). Errors must be within specified margins:
   * Mean Error < 1\%.
   * Variance Error < 5\%.
### Memory Management & Forgetting (Tri-Anchor Model)
 * **LMK Response:** Simulates LMK pressure via onMemoryPressure().
 * **Context Continuity:** Logic verifies token contexts and IDs remain intact throughout KV-cache compression.
 * **Pruning Logic:** Tokens move between Resolved zones based on token volume or LMK pressure signals.
 * **Natural Forgetting (L3 Deep-store):** Implements decay-based pruning using the stability formula: S(t) = e^{-\lambda t}. Low-priority memories with stability < 0.1 are pruned.
## 3. Data Structures
### Binary Checkpoint Schema (FlatBuffers)
Designed for zero-copy deserialization and aligned access:
 * **Alignment:** 16-byte alignment of INT8 vector data is mandatory for NEON optimization.
 * **KV Cache Storage:** LZ4-compressed blocks of KV data. (Note: Implied by prompts not shown in images? No, keep to images. Images mention LZ4 is intended. prompts also mentioned compressed KV data. I will include this). Differential LZ4 compressed KV cache in shadow buffer.
### Tri-Anchor KV Model Structure
The memory is resolution-degraded across three zones:
 * **Anchor 1 (Prefix/Identity):** System prompt and core identity are pinned via mlock().
 * **Anchor 2 (Compressed Historical):** Summarized, INT8 quantized or LZ4 compressed historical context.
 * **Anchor 3 (Recent Context):** High-precision rolling window of the last N tokens. (Note: pruning logic shifts tokens between these).
### SQLite Deep-store Schema
 * **Memory Storage:** Uses an Entity-Relationship model.
 * **Facts Table:** Includes a priority column (LOW, MEDIUM, HIGH, CRITICAL). BMD5 indexed summary of historical context. (Note: images just mention BMD5 indexing not FTS5. BMD5 is mentioned in prompt text, FTS5 FSearch is mentioned. The AI response doesn't detail it. Image text saysFacts table uses priority column, stores historical context in summaries. I will stick with image text data. It just shows Facts table with priority column. Pruning summarized context to Deep-store). (Image text: summarizes historical context, stores Entity-Relationship memory. facts table uses priority column LOW, MED, HIGH, CRITICAL. Pruningsummarized contents from Anchor 2).
## 4. Platform Constraints
### Mobile Hardware Targeting
 * **Architecture:** Snapdragon 778G / Cortex-A78 cores.
 * **Prerequisites:** ARMv8.2-A Dot Product extension is required for vectorized operations.
 * **Thermal Ceilings:** The kernel must shift to scalar paths on SEVERE global thermal state to prevent CPU frequency collapse.
### Operating System (Android)
 * **LMK Memory Pressure:** The Tri-Anchor model must response to onTrimMemory() signals.
 * **Target API Level:** Target android-26 to natively support memfd_create for shadow buffers.
 * **Memory Residency:** Strict mlock() limits necessitate resolution-degradation memory management rather than full model state pinning.
### Build & Verification (GitHub Actions)
 * **Build Pipeline:** must verify both compatibility (NDK build) and memory safety (Valgrind audit).
 * **Zero-Copy:** JNI boundary requires 16-byte pointer alignment for NEON vld1q_s8 instructions and use of GetDirectBufferAddress.
## 5. Actionable Implementation Steps (C++/Zig)
### Vector Similarity Kernel (src/ronin_compute.cpp)
 1. Initialize global thermal state flag g_thermal_state.
 2. Implement compute_similarity_scalar for fallback.
 3. Implement compute_intent_similarity_neon.
 4. Inside neon function, check g_thermal_state. If SEVERE, jump to scalar.
 5. Load 16 INT8 elements from normalized vectors (use vld1q_s8, require 16-byte aligned pointer).
 6. Use vdotq_s32 SIMD intrinsic to process vectors (16 elements/cycle), accumulating to a 4-lane 32-bit register.
 7. Perform final horizontal reduction using vaddvq_f32.
 8. Normalize result after scaling by 127 \times 127 = 16129.
### JNI Bridge (src/ronin_jni.cpp)
 1. Use GetDirectBufferAddress for Zero-Copy access to INT8 vectors.
 2. Align pointer addresses to 16-bytes: (addr & 15) == 0.
 3. Implement C++ wrapper function to wrap compute_intent_similarity_neon.
 4. JNI signature must match com.ronin.kernel.NativeEngine.
### Long-Term Memory Natural Forgetting Logic
 1. Initialize SQLite Deep-store with Facts table and priority column. BMD5 indexed (Note: images mention BMD5 not FTS5 indexing... Wait. User original prompt images only show BMD5. prompts did mention BMD5 FSearch. But I must stick to the conversation. Conversation prompts showed me my prompt "FSearch". But user prompt says get spec from *this conversation*. The final conversation images only show confirmation of BMD5 indexing and summarized context. prompts mentioned FSearch. But the AI response only confirms BMD5 indexing for deep store. Pruning summarized historical context. prompts asked to create schema for entity-relation memory, summarizing historical context. I'll note that. Pruning summaries. BMD5 indexing not detailed. (Note: image says summarizes historical context, stores Entity-Relationship memory. facts table uses priority column LOW, MED, HIGH, CRITICAL. Pruningsummarized contents from Anchor 2)).
 2. Implement background maintenance loop in src/long_term_memory.cpp.
 3. Scan Deep-store L3 using stability formula S(t) = e^{-\lambda t}. Identify memories below 0.1 stability threshold.
 4. Identify LOW priority memories.
 5. Prune memories meeting criteria.
 6. Ensure maintenance task runs *only* when charging.
 7. Log pruned items to Android system logs.
### Build System & CI Configuration
 1. Create standard Android project structure in android/: settings.gradle, build.gradle, AndroidManifest.xml.
 2. Set app-level build.gradle to use root CMakeLists.txt and include Jetpack Compose dependencies.
 3. Modify build.yml in .github/workflows/:
   * Verify linkage step for POSIX Realtime (rt) library on host builds.
   * Integrate Valgrind with --error-exitcode=1 into test-host job.
   * Add mandatory JNI symbol verification step using readelf on libronin_kernel.so. Explicitly fail build if symbols are missing.
   * Set Android target to android-26 (API 26).
   * Set build-android job to only run if test-host (host build/test) is 100% successful.
   * Include mandatory precision test step in test-host (tests/precision_test.cpp). Ensure Mean Error < 1\%, Variance Error < 5\%.
   * Include mandatory memory pressure stress test step in test-host (tests/memory_stress_test.cpp). (Note: from images... wait. prompt had memory stress test in image 6. I'll list it). mandatory step in test-host (tests/memory_stress_test.cpp).
   * Add final artifact upload step via actions/upload-artifact@v4 for .so (named ronin-kernel-arm64-v8a) and .apk files.
