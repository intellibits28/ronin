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
    
    // v12.5: Hardened 40Hz Low-pass for typical Android Vibration Analysis
    initFilters(100.0f, 40.0f); 
    LOGI(TAG, "ResonanceAnalyzer Hardened initialized (4th Order Butterworth @ 40Hz).");
}

void ResonanceAnalyzer::initFilters(float fs, float fc) {
    float omega = std::tan(M_PI * fc / fs);
    float root2 = std::sqrt(2.0f);
    float norm = 1.0f / (1.0f + root2 * omega + omega * omega);
    
    BiquadCoeffs c;
    c.b0 = omega * omega * norm;
    c.b1 = 2.0f * c.b0;
    c.b2 = c.b0;
    c.a1 = 2.0f * (omega * omega - 1.0f) * norm;
    c.a2 = (1.0f - root2 * omega + omega * omega) * norm;
    
    m_filter_low1.setCoeffs(c);
    m_filter_low2.setCoeffs(c); // Cascade for 4th order
}

void ResonanceAnalyzer::pushSamples(const std::vector<float>& x, const std::vector<float>& y, const std::vector<float>& z) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    std::vector<float> magnitude(m_n_samples);
    for (int i = 0; i < m_n_samples; ++i) {
        float mag = std::sqrt(x[i]*x[i] + y[i]*y[i] + z[i]*z[i]);
        // Phase 2: Apply 4th order IIR filter
        magnitude[i] = m_filter_low2.process(m_filter_low1.process(mag));
    }

    // Pre-processing: Remove DC Offset
    float mean = 0.0f;
    for (float v : magnitude) mean += v;
    mean /= m_n_samples;
    for (float& v : magnitude) v -= mean;

    processBatchWelch(magnitude);
}

void ResonanceAnalyzer::processBatchWelch(const std::vector<float>& magnitude) {
    // v12.6: Simplified Welch (Single high-res batch with Hann windowing)
    std::vector<float> windowed = magnitude;
    for (int i = 0; i < m_n_samples; ++i) {
        float win = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (m_n_samples - 1)));
        windowed[i] *= win;
    }

    float* work = (float*)pffft_aligned_malloc(m_n_samples * sizeof(float));
    float* output = (float*)pffft_aligned_malloc(m_n_samples * sizeof(float));
    
    pffft_transform_ordered(m_pffft_setup, windowed.data(), output, work, PFFFT_FORWARD);

    float max_psd = -100.0f;
    int max_idx = 0;
    float sum_psd = 0.0f;

    for (int i = 1; i < m_n_samples / 2; ++i) {
        float r = output[2*i];
        float im = output[2*i + 1];
        // Power Spectral Density (Magnitude Squared)
        float psd = (r*r + im*im) / m_n_samples;
        float psd_db = 10.0f * std::log10(psd + 1e-12f);
        
        if (psd_db > max_psd) {
            max_psd = psd_db;
            max_idx = i;
        }
        sum_psd += psd;
    }

    float sample_rate = 100.0f; 
    m_last_analysis.resonance_freq_hz = (float)max_idx * sample_rate / m_n_samples;
    m_last_analysis.psd_peak_db = max_psd;
    m_last_analysis.noise_floor_db = 10.0f * std::log10(sum_psd / (m_n_samples/2) + 1e-12f);
    m_last_analysis.sample_count = m_n_samples;
    m_last_analysis.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // v12.7: Dynamic Anomaly Threshold (Peak must be 15dB above average noise floor)
    m_last_analysis.anomaly_detected = (m_last_analysis.psd_peak_db > m_last_analysis.noise_floor_db + 15.0f) && 
                                       (m_last_analysis.psd_peak_db > -40.0f);

    pffft_aligned_free(work);
    pffft_aligned_free(output);
}


std::string ResonanceAnalyzer::getAnalysisJson(const std::string& sensor_type) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
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
