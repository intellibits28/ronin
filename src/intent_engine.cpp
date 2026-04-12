#include "intent_engine.h"

#ifdef __aarch64__
#include <arm_neon.h>
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif

#include <iostream>
#include "ronin_log.h"

#define TAG "RoninIntentEngine"

namespace Ronin::Kernel::Intent {

// Initialize to NORMAL by default
ThermalState g_thermal_state = ThermalState::NORMAL;

/**
 * Scalar fallback: Calculating dot product to minimize power usage in SEVERE thermal state.
 * Also used as host-side implementation for CI/CD on x86_64 or older ARM devices.
 */
static float compute_similarity_scalar(const int8_t* a, const int8_t* b) {
    int32_t dot_product = 0;
    for (int i = 0; i < 128; ++i) {
        dot_product += static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]);
    }
    // Normalize based on INT8 max (127^2) to keep range [-1, 1]
    return static_cast<float>(dot_product) / 16129.0f;
}

#ifdef __aarch64__
/**
 * Internal helper to check if the current CPU supports ARMv8.2 Dot Product extension.
 */
static bool supports_dot_product() {
    static bool checked = false;
    static bool supported = false;
    if (!checked) {
        unsigned long hwcaps = getauxval(AT_HWCAP);
        supported = (hwcaps & HWCAP_ASIMDDP);
        checked = true;
        if (supported) {
            LOGI(TAG, "Hardware support detected: ASIMD Dot Product extension active.");
        } else {
            LOGI(TAG, "Hardware support not found: ASIMD Dot Product extension disabled.");
        }
    }
    return supported;
}
#endif

/**
 * Intent Similarity calculation.
 * Uses ARM64 NEON SIMD on mobile, with a scalar fallback for thermal throttling,
 * older hardware, or non-ARM hosts.
 */
float compute_intent_similarity_neon(const int8_t* a, const int8_t* b) {
    // 1. Non-ARM host fallback
#ifndef __aarch64__
    return compute_similarity_scalar(a, b);
#else
    // 2. Runtime Hardware Check: vdotq_s32 requires ARMv8.2-A DotProd
    // 3. Thermal Check: Fallback if SEVERE
    if (!supports_dot_product() || g_thermal_state == ThermalState::SEVERE) {
        return compute_similarity_scalar(a, b);
    }

    // Initialize accumulators with 0s
    int32x4_t acc = vdupq_n_s32(0);

    // Process 128 elements in chunks of 16 (128 / 16 = 8 iterations)
    for (int i = 0; i < 128; i += 16) {
        int8x16_t va = vld1q_s8(a + i);
        int8x16_t vb = vld1q_s8(b + i);
        // vdotq_s32 is extremely efficient on modern ARMv8-A (Cortex-A78/A55)
        acc = vdotq_s32(acc, va, vb);
    }

    // Convert int32x4_t to float32x4_t and perform final reduction
    float32x4_t f_acc = vcvtq_f32_s32(acc);
    float final_sum = vaddvq_f32(f_acc);

    return final_sum / 16129.0f;
#endif
}

} // namespace Ronin::Kernel::Intent
