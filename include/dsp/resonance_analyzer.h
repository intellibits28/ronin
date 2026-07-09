#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <shared_mutex>
#include "third_party/pffft/pffft.h"

namespace Ronin::Kernel::DSP {

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
