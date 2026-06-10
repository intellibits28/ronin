#include "dsp/resonance_analyzer.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include "ronin_log.h"
#include <nlohmann/json.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "RoninDSP"

namespace Ronin::Kernel::DSP {

ResonanceAnalyzer::ResonanceAnalyzer(int n_samples) : m_n_samples(n_samples) {
    m_pffft_setup = pffft_new_setup(n_samples, PFFFT_REAL);
    m_buffer_mag.resize(n_samples, 0.0f);
    LOGI(TAG, "ResonanceAnalyzer initialized with %d samples.", n_samples);
}

ResonanceAnalyzer::~ResonanceAnalyzer() {
    if (m_pffft_setup) pffft_destroy_setup(m_pffft_setup);
}

void ResonanceAnalyzer::pushSamples(const std::vector<float>& x, const std::vector<float>& y, const std::vector<float>& z) {
    std::unique_lock lock(m_mutex);
    
    // v1.0: Vector Magnitude Calculation (Root Sum of Squares)
    std::vector<float> magnitude(m_n_samples);
    for (int i = 0; i < m_n_samples; ++i) {
        magnitude[i] = std::sqrt(x[i]*x[i] + y[i]*y[i] + z[i]*z[i]);
    }

    // Pre-processing: Remove DC Offset (Gravity)
    float mean = 0.0f;
    for (float v : magnitude) mean += v;
    mean /= m_n_samples;
    for (float& v : magnitude) v -= mean;

    // Windowing (Hann Window)
    for (int i = 0; i < m_n_samples; ++i) {
        float window = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (m_n_samples - 1)));
        magnitude[i] *= window;
    }

    // FFT Execution
    float* work = (float*)pffft_aligned_malloc(m_n_samples * sizeof(float));
    float* output = (float*)pffft_aligned_malloc(m_n_samples * sizeof(float));
    
    pffft_transform_ordered(m_pffft_setup, magnitude.data(), output, work, PFFFT_FORWARD);

    // PSD Peak Detection
    float max_mag = 0.0f;
    int max_idx = 0;
    // PFFFT output for REAL: [R0, R(N/2), R1, I1, R2, I2, ... R(N/2-1), I(N/2-1)]
    // We care about Rk^2 + Ik^2
    for (int i = 1; i < m_n_samples / 2; ++i) {
        float r = output[2*i];
        float im = output[2*i + 1];
        float mag = std::sqrt(r*r + im*im);
        if (mag > max_mag) {
            max_mag = mag;
            max_idx = i;
        }
    }

    // Assume 100Hz sampling rate for Mi 11 Lite 5G NE FASTEST
    float sample_rate = 100.0f; 
    m_last_analysis.resonance_freq_hz = (float)max_idx * sample_rate / m_n_samples;
    m_last_analysis.psd_peak_db = 20.0f * std::log10(max_mag + 1e-9f);
    m_last_analysis.sample_count = m_n_samples;
    m_last_analysis.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_last_analysis.anomaly_detected = (m_last_analysis.psd_peak_db > -20.0f); // Tweakable threshold

    pffft_aligned_free(work);
    pffft_aligned_free(output);
}

std::string ResonanceAnalyzer::getAnalysisJson(const std::string& sensor_type) {
    std::shared_lock lock(m_mutex);
    nlohmann::json j;
    j["resonance_freq_hz"] = m_last_analysis.resonance_freq_hz;
    j["psd_peak_db"] = m_last_analysis.psd_peak_db;
    j["noise_floor_db"] = -75.0f; // Baseline for now
    j["anomaly_detected"] = m_last_analysis.anomaly_detected;
    j["sample_count"] = m_last_analysis.sample_count;
    j["timestamp_ms"] = m_last_analysis.timestamp_ms;
    return j.dump();
}

} // namespace Ronin::Kernel::DSP
