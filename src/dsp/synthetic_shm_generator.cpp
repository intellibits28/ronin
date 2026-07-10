#include "dsp/synthetic_shm_generator.h"
#include <cmath>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ronin::Kernel::DSP {

SyntheticShmGenerator::SyntheticShmGenerator(float sample_rate_hz, uint32_t window_size)
    : m_sample_rate(sample_rate_hz), m_window_size(window_size) {
    reset();
}

void SyntheticShmGenerator::reset() {
    m_profile = GeneratorProfile::CLEAN_BASELINE;
    m_base_f0_x = 5.0f;
    m_base_f0_y = 5.15f;
    m_base_f0_z = 5.3f;
    m_current_f0_x = m_base_f0_x;
    m_current_f0_y = m_base_f0_y;
    m_current_f0_z = m_base_f0_z;
    m_noise_sigma = 0.05f;
    m_drift_rate = 0.0f;
    m_has_impact = false;
    m_impact_strength = 12.0f;
    m_impact_decay = 8.0f;
    m_window_index = 0;
}
void SyntheticShmGenerator::setProfile(GeneratorProfile profile) {
    m_profile = profile;
    if (profile == GeneratorProfile::CLEAN_BASELINE) {
        m_noise_sigma = 0.05f;
        m_drift_rate = 0.0f;
        m_has_impact = false;
    } else if (profile == GeneratorProfile::IMPULSE_BURST_HIGH_NOISE) {
        m_noise_sigma = 1.2f;
        m_drift_rate = 0.0f;
        m_has_impact = true;
    } else if (profile == GeneratorProfile::STRUCTURAL_SHIFT_DRIFT) {
        m_noise_sigma = 0.05f;
        m_drift_rate = -0.05f;
        m_has_impact = false;
    }
}

void SyntheticShmGenerator::setBaselineF0(float f0_x, float f0_y, float f0_z) {
    m_base_f0_x = f0_x;
    m_base_f0_y = f0_y;
    m_base_f0_z = f0_z;
    m_current_f0_x = f0_x;
    m_current_f0_y = f0_y;
    m_current_f0_z = f0_z;
}

void SyntheticShmGenerator::setNoiseSigma(float sigma) {
    m_noise_sigma = sigma;
}

void SyntheticShmGenerator::setDriftRate(float drift_hz_per_sec) {
    m_drift_rate = drift_hz_per_sec;
}

void SyntheticShmGenerator::triggerImpact(float strength, float decay) {
    m_has_impact = true;
    m_impact_strength = strength;
    m_impact_decay = decay;
}

void SyntheticShmGenerator::setFrequencyStep(float step_hz_x, float step_hz_y, float step_hz_z) {
    m_current_f0_x = step_hz_x;
    m_current_f0_y = step_hz_y;
    m_current_f0_z = step_hz_z;
}
float SyntheticShmGenerator::generateGaussianNoise(float mean, float stddev) {
    static std::mt19937 gen(42 + m_window_index);
    std::normal_distribution<float> dist(mean, stddev);
    return dist(gen);
}

GeneratedSignal SyntheticShmGenerator::generateNextWindow() {
    GeneratedSignal signal;
    signal.x.resize(m_window_size);
    signal.y.resize(m_window_size);
    signal.z.resize(m_window_size);
    float amp = 1.5f;
    if (m_drift_rate != 0.0f) {
        float duration_sec = static_cast<float>(m_window_size) / m_sample_rate;
        m_current_f0_x += m_drift_rate * duration_sec;
        m_current_f0_y += m_drift_rate * duration_sec;
        m_current_f0_z += m_drift_rate * duration_sec;
    }
    for (uint32_t i = 0; i < m_window_size; ++i) {
        float t = static_cast<float>(i) / m_sample_rate;
        float noise_x = generateGaussianNoise(0.0f, m_noise_sigma);
        float noise_y = generateGaussianNoise(0.0f, m_noise_sigma);
        float noise_z = generateGaussianNoise(0.0f, m_noise_sigma);
        if (m_has_impact) {
            signal.x[i] = m_impact_strength * std::exp(-m_impact_decay * t) * std::sin(2.0f * M_PI * m_current_f0_x * t) + noise_x;
            signal.y[i] = m_impact_strength * std::exp(-m_impact_decay * t) * std::sin(2.0f * M_PI * m_current_f0_y * t) + noise_y;
            signal.z[i] = m_impact_strength * std::exp(-m_impact_decay * t) * std::sin(2.0f * M_PI * m_current_f0_z * t) + noise_z;
        } else {
            signal.x[i] = amp * std::sin(2.0f * M_PI * m_current_f0_x * t) + noise_x;
            signal.y[i] = amp * std::sin(2.0f * M_PI * m_current_f0_y * t) + noise_y;
            signal.z[i] = amp * std::sin(2.0f * M_PI * m_current_f0_z * t) + noise_z;
        }
    }
    signal.current_f0_x = m_current_f0_x;
    signal.current_f0_y = m_current_f0_y;
    signal.current_f0_z = m_current_f0_z;
    m_window_index++;
    if (m_has_impact) {
        m_has_impact = false;
    }
    return signal;
}

} // namespace Ronin::Kernel::DSP
