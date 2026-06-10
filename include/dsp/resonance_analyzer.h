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

class ResonanceAnalyzer {
public:
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

    void processBatch();
    float calculatePSD(const std::vector<float>& samples);
};

} // namespace Ronin::Kernel::DSP
