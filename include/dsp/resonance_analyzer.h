#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <cmath>
#include <algorithm>
#include "third_party/pffft/pffft.h"

namespace Ronin::Kernel::DSP {

/**
 * Phase 4 Active Inference: Lock-Free Precision-Weighted Audio Kalman Filter
 * Executes inline on the real-time audio/DSP thread with zero heap allocations
 * and zero mutex contention against ExpectedFreeEnergyRouter.
 */
struct AudioKalmanUpdate {
    float posterior_freq_hz = 0.0f;
    float posterior_precision = 1.0f;
    float sensory_precision = 1.0f;
    float kalman_gain = 0.5f;
    float prediction_error = 0.0f;
};

struct SensorAnalysis {
    float resonance_freq_hz = 0.0f;
    float psd_peak_db = 0.0f;
    float noise_floor_db = 0.0f;
    bool anomaly_detected = false;
    uint32_t sample_count = 0;
    uint64_t timestamp_ms = 0;
};

// Phase 2: Biquad IIR Filter Section
struct BiquadCoeffs {
    float b0, b1, b2, a1, a2;
};

class BiquadFilter {
public:
    BiquadFilter() { reset(); }
    void setCoeffs(const BiquadCoeffs& c) { m_c = c; }
    float process(float x) {
        float y = m_c.b0 * x + m_c.b1 * m_z1 + m_c.b2 * m_z2 - m_c.a1 * m_v1 - m_c.a2 * m_v2;
        m_z2 = m_z1; m_z1 = x;
        m_v2 = m_v1; m_v1 = y;
        return y;
    }
    void reset() { m_z1 = m_z2 = m_v1 = m_v2 = 0.0f; }
private:
    BiquadCoeffs m_c;
    float m_z1, m_z2, m_v1, m_v2;
};

class ResonanceAnalyzer {
public:
    static ResonanceAnalyzer& getInstance();
    ResonanceAnalyzer(int n_samples = 1024);
    ~ResonanceAnalyzer();

    void pushSamples(const std::vector<float>& x, const std::vector<float>& y, const std::vector<float>& z);
    std::string getAnalysisJson(const std::string& sensor_type);

    /**
     * Phase 4 Active Inference: Lock-free inline precision-weighted Kalman update.
     * Pi_audio = exp(-beta * max(0, noise_rms))
     * K_t = Pi_audio / (Pi_audio + Pi_prior)
     */
    static inline AudioKalmanUpdate computeAudioKalmanPrecision(
        float freq_obs_hz,
        float prior_freq_hz,
        float prior_precision,
        float noise_rms,
        float beta = 5.0f) noexcept {
        AudioKalmanUpdate out;
        const float safe_noise = std::max(0.0f, noise_rms);
        const float safe_prior_pi = std::max(1e-4f, prior_precision);
        const float pi_audio = std::exp(-beta * safe_noise);
        const float k_gain = pi_audio / (pi_audio + safe_prior_pi);
        const float pred_err = freq_obs_hz - prior_freq_hz;

        out.sensory_precision = pi_audio;
        out.kalman_gain = k_gain;
        out.prediction_error = pred_err;
        out.posterior_freq_hz = prior_freq_hz + k_gain * pred_err;
        // Bound posterior precision to avoid precision saturation over long streams
        out.posterior_precision = std::clamp(safe_prior_pi + pi_audio * 0.25f, 1e-3f, 10.0f);
        return out;
    }

private:
    int m_n_samples;
    PFFFT_Setup* m_pffft_setup;
    
    std::vector<float> m_buffer_mag;
    mutable std::shared_mutex m_mutex;
    SensorAnalysis m_last_analysis;

    // SHM: Dynamic noise floor tracking
    static constexpr size_t NF_RING_CAPACITY = 64;
    float m_noise_floor_ring[NF_RING_CAPACITY];
    size_t m_nf_head;
    size_t m_nf_size;
    float calculateDynamicNoiseFloor() const;

    // Phase 2 components
    BiquadFilter m_filter_low1, m_filter_low2; // Cascaded for 4th order
    void initFilters(float sample_rate, float cutoff_hz);
    
    void processBatchWelch(const std::vector<float>& magnitude);
};

} // namespace Ronin::Kernel::DSP
