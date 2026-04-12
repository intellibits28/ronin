#include "intent_engine.h"

#ifdef __aarch64__
#include <arm_neon.h>
#endif

#include <iostream>

namespace Ronin::Kernel::Intent {

// Initialize to NORMAL by default
ThermalState g_thermal_state = ThermalState::NORMAL;

/**
 * Scalar fallback: Calculating dot product to minimize power usage in SEVERE thermal state.
 * Also used as host-side implementation for CI/CD on x86_64.
 */
static float compute_similarity_scalar(const int8_t* a, const int8_t* b) {
    int32_t dot_product = 0;
    for (int i = 0; i < 128; ++i) {
        dot_product += static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]);
    }
    // Normalize based on INT8 max (127^2) to keep range [-1, 1]
    return static_cast<float>(dot_product) / 16129.0f;
}

/**
 * Intent Similarity calculation.
 * Uses ARM64 NEON SIMD on mobile, with a scalar fallback for thermal throttling or non-ARM hosts.
 */
float compute_intent_similarity_neon(const int8_t* a, const int8_t* b) {
    // Thermal-aware fallback or Non-ARM host fallback
#ifndef __aarch64__
    return compute_similarity_scalar(a, b);
#else
    if (g_thermal_state == ThermalState::SEVERE) {
        return compute_similarity_scalar(a, b);
    }

    // Initialize accumulators with 0s
    int32x4_t acc = vdupq_n_s32(0);

    // Process 128 elements in chunks of 16 (128 / 16 = 8 iterations)
    for (int i = 0; i < 128; i += 16) {
        int8x16_t va = vld1q_s8(a + i);
        int8x16_t vb = vld1q_s8(b + i);
        // vdotq_s32 is extremely efficient on modern ARMv8-A
        acc = vdotq_s32(acc, va, vb);
    }

    // Convert int32x4_t to float32x4_t and perform final reduction
    float32x4_t f_acc = vcvtq_f32_s32(acc);
    float final_sum = vaddvq_f32(f_acc);

    return final_sum / 16129.0f;
#endif
}

} // namespace Ronin::Kernel::Intent
