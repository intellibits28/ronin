#pragma once

#include <vector>
#include <string>

namespace Ronin::Kernel::DSP {

enum class GeneratorProfile {
    CLEAN_BASELINE,
    IMPULSE_BURST_HIGH_NOISE,
    STRUCTURAL_SHIFT_DRIFT
};

struct GeneratedSignal {
    std::vector<float> x;
    std::vector<float> y;
    std::vector<float> z;
    float current_f0_x;
    float current_f0_y;
    float current_f0_z;
};

class SyntheticShmGenerator {
public:
    SyntheticShmGenerator(float sample_rate_hz = 100.0f, uint32_t window_size = 1024);

    void setProfile(GeneratorProfile profile);
    void setBaselineF0(float f0_x, float f0_y, float f0_z);
    void setNoiseSigma(float sigma);
    void setDriftRate(float drift_hz_per_sec);
    void triggerImpact(float strength = 12.0f, float decay = 8.0f);
    void setFrequencyStep(float step_hz_x, float step_hz_y, float step_hz_z);

    GeneratedSignal generateNextWindow();
    void reset();

private:
    float m_sample_rate;
    uint32_t m_window_size;
    GeneratorProfile m_profile;
    
    float m_base_f0_x;
    float m_base_f0_y;
    float m_base_f0_z;
    
    float m_current_f0_x;
    float m_current_f0_y;
    float m_current_f0_z;

    float m_noise_sigma;
    float m_drift_rate;
    
    bool m_has_impact;
    float m_impact_strength;
    float m_impact_decay;

    uint64_t m_window_index;
    
    float generateGaussianNoise(float mean, float stddev);
};

} // namespace Ronin::Kernel::DSP
