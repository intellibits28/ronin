#pragma once

#include <cstdint>

namespace Ronin::Kernel::Intent {

enum class ThermalState {
    NORMAL,
    MODERATE,
    SEVERE
};

// Global thermal state (should be updated by Android PowerManager/Thermal HAL)
extern ThermalState g_thermal_state;

/**
 * Calculates cosine similarity between two 128-dim INT8 pre-normalized vectors.
 * Uses NEON SIMD with thermal-aware fallback.
 */
float compute_intent_similarity_neon(const int8_t* a, const int8_t* b);

/**
 * Calculates cosine similarity between two float vectors.
 * Optimized with NEON SIMD for mobile performance.
 */
float compute_cosine_similarity_neon(const float* a, const float* b, size_t length);

} // namespace Ronin::Kernel::Intent
