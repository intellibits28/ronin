#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cmath>
#include <memory>
#include "third_party/pffft/pffft.h"

namespace Ronin::Kernel::DSP {

enum class AnalysisMode {
    TIME_DOMAIN,
    FREQUENCY_DOMAIN
};

enum class KernelSensorState {
    IDLE,
    STARTUP,
    STABLE,
    SHUTDOWN
};

struct AdaptiveSamplingProfile {
    std::string profile_name;
    float sample_rate_hz;
    uint32_t window_size;
    AnalysisMode mode;
    float high_pass_cutoff_hz;
    float dynamic_std_dev_multiplier;
};

// Biquad High-Pass filter for sensor drift & DC offset removal
class HighPassBiquad {
public:
    HighPassBiquad();
    void configure(float sample_rate_hz, float cutoff_hz);
    float process(float sample);
    void reset();
private:
    float b0, b1, b2, a1, a2;
    float z1, z2;
};

// SamplerController managing sampling profiles, multi-resolution settings, and dynamic thresholding
class SamplerController {
public:
    SamplerController();

    void setProfile(const AdaptiveSamplingProfile& profile);
    const AdaptiveSamplingProfile& getActiveProfile() const;

    void transitionToState(KernelSensorState new_state);
    KernelSensorState getCurrentState() const;
    std::string getStateString() const;

    // Moving Standard Deviation dynamic thresholding
    void pushSignalMetric(float metric);
    float calculateMovingMean() const;
    float calculateMovingStdDev() const;
    float getDynamicThreshold() const;
    void resetMetrics();

private:
    KernelSensorState m_current_state;
    AdaptiveSamplingProfile m_active_profile;

    // Fixed-capacity circular buffer optimized for low memory footprint on mobile
    static constexpr size_t RING_CAPACITY = 32;
    float m_metric_ring[RING_CAPACITY];
    size_t m_ring_head;
    size_t m_ring_size;
    mutable std::mutex m_mutex;
};

struct VibeMonitorResult {
    std::string state;
    std::string profile_name;
    float sample_rate_hz;
    uint32_t window_size;
    std::string analysis_mode;
    bool dc_removed;
    float high_pass_cutoff_hz;
    float moving_mean;
    float moving_std_dev;
    float dynamic_threshold;
    float current_metric;
    bool anomaly_detected;
    float resonance_freq_hz;
    float psd_peak_db;
    std::string summary;
};

// VibeMonitor Engine implementing scenario-based sensor analysis
class VibeMonitorEngine {
public:
    static VibeMonitorEngine& getInstance();
    VibeMonitorEngine();
    ~VibeMonitorEngine();

    SamplerController& getController();

    // Main execution pipeline with DC removal, high-pass filtering, multi-resolution FFT/Time-domain
    VibeMonitorResult analyzePipeline(const std::vector<float>& x, 
                                      const std::vector<float>& y, 
                                      const std::vector<float>& z);

    std::string executeCommandJson(const std::string& command_json);

private:
    SamplerController m_controller;
    HighPassBiquad m_hp_filter;
    float m_configured_hp_cutoff;
    float m_configured_sample_rate;

    // Reusable aligned PFFFT setup cache to minimize memory reallocation
    PFFFT_Setup* m_pffft_setup;
    uint32_t m_pffft_size;

    void ensurePffftSetup(uint32_t size);
    std::mutex m_engine_mutex;
};

} // namespace Ronin::Kernel::DSP
