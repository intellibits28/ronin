#include "dsp/vibe_monitor.h"
#include <algorithm>
#include <sstream>
#include <cstring>
#include <nlohmann/json.hpp>
#include "ronin_log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "RoninVibeMonitor"

namespace Ronin::Kernel::DSP {

HighPassBiquad::HighPassBiquad() {
    reset();
    b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
}

void HighPassBiquad::configure(float sample_rate_hz, float cutoff_hz) {
    // SHM: Clamp cutoff to safe range for low-frequency structural monitoring
    float safe_cutoff = std::max(0.05f, std::min(cutoff_hz, sample_rate_hz * 0.45f));
    if (sample_rate_hz <= 0.0f || safe_cutoff <= 0.0f) {
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
        return;
    }
    float w0 = 2.0f * (float)M_PI * safe_cutoff / sample_rate_hz;
    float cos_w0 = std::cos(w0);
    float alpha = std::sin(w0) / std::sqrt(2.0f);

    float a0 = 1.0f + alpha;
    b0 = ((1.0f + cos_w0) / 2.0f) / a0;
    b1 = (-(1.0f + cos_w0)) / a0;
    b2 = ((1.0f + cos_w0) / 2.0f) / a0;
    a1 = (-2.0f * cos_w0) / a0;
    a2 = (1.0f - alpha) / a0;
}

float HighPassBiquad::process(float x) {
    float y = b0 * x + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;
    z2 = z1;
    z1 = y;
    return y;
}

void HighPassBiquad::reset() {
    z1 = 0.0f;
    z2 = 0.0f;
}

BandPassBiquad::BandPassBiquad() {
    reset();
    b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
}

void BandPassBiquad::configure(float sample_rate_hz, float low_hz, float high_hz) {
    if (sample_rate_hz <= 0.0f || low_hz <= 0.0f || high_hz <= low_hz || high_hz >= sample_rate_hz * 0.5f) {
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
        return;
    }
    float center_hz = std::sqrt(low_hz * high_hz);
    float bw_hz = high_hz - low_hz;
    float w0 = 2.0f * (float)M_PI * center_hz / sample_rate_hz;
    float alpha = std::sin(w0) * (bw_hz / (2.0f * center_hz));

    float a0 = 1.0f + alpha;
    b0 = alpha / a0;
    b1 = 0.0f;
    b2 = -alpha / a0;
    a1 = (-2.0f * std::cos(w0)) / a0;
    a2 = (1.0f - alpha) / a0;
}

float BandPassBiquad::process(float x) {
    float y = b0 * x + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;
    z2 = z1;
    z1 = y;
    return y;
}

void BandPassBiquad::reset() {
    z1 = 0.0f;
    z2 = 0.0f;
}

SamplerController::SamplerController() : m_current_state(KernelSensorState::IDLE), m_ring_head(0), m_ring_size(0) {
    for (size_t i = 0; i < RING_CAPACITY; ++i) m_metric_ring[i] = 0.0f;
    m_tuning_profile = {InstrumentType::NONE, "NONE", "NONE", 0.0f, 0.0f, 0.0f};
    // SHM: Initialize noise floor ring buffer and baseline tracking
    for (size_t i = 0; i < RING_CAPACITY; ++i) m_noise_floor_ring[i] = -75.0f;
    m_nf_head = 0;
    m_nf_size = 0;
    m_baseline_f0 = 0.0f;
    m_baseline_valid = false;
    m_post_impulse_settle_count = 0;
    transitionToState(KernelSensorState::IDLE);
}

void SamplerController::setTargetTuningFrequency(float target_hz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (target_hz <= 0.0f) {
        m_tuning_profile = {InstrumentType::NONE, "NONE", "NONE", 0.0f, 0.0f, 0.0f};
        return;
    }
    std::string name = "CUSTOM";
    InstrumentType inst = InstrumentType::CUSTOM;
    std::string inst_name = "CUSTOM";

    if (std::abs(target_hz - 329.63f) < 5.0f) { name = "E4"; inst = InstrumentType::GUITAR; inst_name = "GUITAR"; }
    else if (std::abs(target_hz - 246.94f) < 5.0f) { name = "B3"; inst = InstrumentType::GUITAR; inst_name = "GUITAR"; }
    else if (std::abs(target_hz - 196.00f) < 5.0f) { name = "G3"; inst = InstrumentType::GUITAR; inst_name = "GUITAR"; }
    else if (std::abs(target_hz - 146.83f) < 5.0f) { name = "D3"; inst = InstrumentType::GUITAR; inst_name = "GUITAR"; }
    else if (std::abs(target_hz - 110.00f) < 5.0f) { name = "A2"; inst = InstrumentType::GUITAR; inst_name = "GUITAR"; }
    else if (std::abs(target_hz - 82.41f) < 5.0f) { name = "E2"; inst = InstrumentType::GUITAR; inst_name = "GUITAR"; }
    else if (std::abs(target_hz - 440.00f) < 5.0f) { name = "A4"; inst = InstrumentType::VIOLIN; inst_name = "VIOLIN"; }
    else if (std::abs(target_hz - 659.25f) < 5.0f) { name = "E5"; inst = InstrumentType::VIOLIN; inst_name = "VIOLIN"; }
    else if (std::abs(target_hz - 261.63f) < 5.0f) { name = "C4"; inst = InstrumentType::UKULELE; inst_name = "UKULELE"; }
    else if (std::abs(target_hz - 392.00f) < 5.0f) { name = "G4"; inst = InstrumentType::UKULELE; inst_name = "UKULELE"; }
    else if (std::abs(target_hz - 41.20f) < 3.0f) { name = "E1"; inst = InstrumentType::BASS; inst_name = "BASS"; }
    else if (std::abs(target_hz - 55.00f) < 3.0f) { name = "A1"; inst = InstrumentType::BASS; inst_name = "BASS"; }
    else if (std::abs(target_hz - 73.42f) < 3.0f) { name = "D2"; inst = InstrumentType::BASS; inst_name = "BASS"; }
    else if (std::abs(target_hz - 98.00f) < 3.0f) { name = "G2"; inst = InstrumentType::BASS; inst_name = "BASS"; }

    float low_hz = target_hz * 0.91f;
    float high_hz = target_hz * 1.21f;
    if (name == "E4") { low_hz = 300.0f; high_hz = 400.0f; }

    m_tuning_profile = {inst, inst_name, name, target_hz, low_hz, high_hz};
    float new_rate = std::max(2000.0f, target_hz * 4.5f);
    m_active_profile.sample_rate_hz = new_rate;
    LOGI(TAG, "Dynamic Tuning Profile configured for %s:%s (Target: %.2fHz, Bandpass: %.1f-%.1fHz, Rate: %.1fHz)",
         inst_name.c_str(), name.c_str(), target_hz, low_hz, high_hz, new_rate);
}

void SamplerController::setInstrumentStringProfile(InstrumentType instrument, const std::string& string_name) {
    std::string s = string_name;
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    if (instrument == InstrumentType::NONE || s == "NONE" || s == "OFF") {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tuning_profile = {InstrumentType::NONE, "NONE", "NONE", 0.0f, 0.0f, 0.0f};
        return;
    }

    float target_hz = 0.0f;
    std::string inst_name = "CUSTOM";
    if (instrument == InstrumentType::GUITAR) {
        inst_name = "GUITAR";
        if (s == "E4") target_hz = 329.63f;
        else if (s == "B3") target_hz = 246.94f;
        else if (s == "G3") target_hz = 196.00f;
        else if (s == "D3") target_hz = 146.83f;
        else if (s == "A2") target_hz = 110.00f;
        else if (s == "E2") target_hz = 82.41f;
    } else if (instrument == InstrumentType::VIOLIN) {
        inst_name = "VIOLIN";
        if (s == "G3") target_hz = 196.00f;
        else if (s == "D4") target_hz = 293.66f;
        else if (s == "A4") target_hz = 440.00f;
        else if (s == "E5") target_hz = 659.25f;
    } else if (instrument == InstrumentType::UKULELE) {
        inst_name = "UKULELE";
        if (s == "C4") target_hz = 261.63f;
        else if (s == "E4") target_hz = 329.63f;
        else if (s == "G4") target_hz = 392.00f;
        else if (s == "A4") target_hz = 440.00f;
    } else if (instrument == InstrumentType::BASS) {
        inst_name = "BASS";
        if (s == "E1") target_hz = 41.20f;
        else if (s == "A1") target_hz = 55.00f;
        else if (s == "D2") target_hz = 73.42f;
        else if (s == "G2") target_hz = 98.00f;
    }

    if (target_hz > 0.0f) {
        setTargetTuningFrequency(target_hz);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tuning_profile.instrument = instrument;
        m_tuning_profile.instrument_name = inst_name;
        m_tuning_profile.string_name = s;
    }
}

TuningProfile SamplerController::getActiveTuningProfile() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tuning_profile;
}

void SamplerController::setProfile(const AdaptiveSamplingProfile& profile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active_profile = profile;
    LOGI(TAG, "Active Sampling Profile switched to: %s (Rate: %.1fHz, Window: %u, HP Cutoff: %.2fHz)",
         profile.profile_name.c_str(), profile.sample_rate_hz, profile.window_size, profile.high_pass_cutoff_hz);
}

const AdaptiveSamplingProfile& SamplerController::getActiveProfile() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active_profile;
}

void SamplerController::transitionToState(KernelSensorState new_state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_current_state = new_state;

    switch (new_state) {
        case KernelSensorState::IDLE:
            m_active_profile = {"IDLE_STANDBY", 20.0f, 256, AnalysisMode::TIME_DOMAIN, 0.1f, 2.5f};
            break;
        case KernelSensorState::STARTUP:
            // Multi-Resolution Support: Structural analysis (e.g. 1024 samples at 100Hz for 10.2s window)
            m_active_profile = {"STRUCTURAL_RESONANCE", 100.0f, 1024, AnalysisMode::FREQUENCY_DOMAIN, 1.0f, 3.0f};
            break;
        case KernelSensorState::STABLE:
            if (m_baseline_valid) {
                // Settle back to structural monitoring profile if a structural baseline is established
                m_active_profile = {"STRUCTURAL_RESONANCE", 100.0f, 1024, AnalysisMode::FREQUENCY_DOMAIN, 1.0f, 3.0f};
            } else {
                // Multi-Resolution Support: High-frequency machine diagnostics (e.g. 1024 samples at 200Hz for Motor/Compressor)
                m_active_profile = {"MACHINE_DIAGNOSTICS", 200.0f, 1024, AnalysisMode::FREQUENCY_DOMAIN, 0.5f, 3.0f};
            }
            break;
        case KernelSensorState::IMPULSE_MODE:
            // High-resolution burst capture (1024 samples at 200Hz for FFT impulse decay analysis)
            m_active_profile = {"IMPULSE_CAPTURE", 200.0f, 1024, AnalysisMode::FREQUENCY_DOMAIN, 0.5f, 3.0f};
            break;
        case KernelSensorState::SHUTDOWN:
            m_active_profile = {"SHUTDOWN_DECAY", 50.0f, 512, AnalysisMode::TIME_DOMAIN, 0.5f, 2.0f};
            break;
    }
    LOGI(TAG, "State transition to %s -> Active Profile: %s", 
         m_active_profile.profile_name.c_str(), m_active_profile.profile_name.c_str());
}

KernelSensorState SamplerController::getCurrentState() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current_state;
}

std::string SamplerController::getStateString() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    switch (m_current_state) {
        case KernelSensorState::IDLE: return "IDLE";
        case KernelSensorState::STARTUP: return "STARTUP";
        case KernelSensorState::STABLE: return "STABLE";
        case KernelSensorState::IMPULSE_MODE: return "IMPULSE_MODE";
        case KernelSensorState::SHUTDOWN: return "SHUTDOWN";
        default: return "UNKNOWN";
    }
}

void SamplerController::pushSignalMetric(float metric) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_metric_ring[m_ring_head] = metric;
    m_ring_head = (m_ring_head + 1) % RING_CAPACITY;
    if (m_ring_size < RING_CAPACITY) {
        m_ring_size++;
    }
}

float SamplerController::calculateMovingMeanLocked() const {
    if (m_ring_size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < m_ring_size; ++i) {
        sum += m_metric_ring[i];
    }
    return sum / (float)m_ring_size;
}

float SamplerController::calculateMovingStdDevLocked() const {
    if (m_ring_size <= 1) return 0.0f;
    float mean = calculateMovingMeanLocked();
    float var_sum = 0.0f;
    for (size_t i = 0; i < m_ring_size; ++i) {
        float diff = m_metric_ring[i] - mean;
        var_sum += diff * diff;
    }
    // Unbiased sample standard deviation using Bessel's correction (N - 1)
    return std::sqrt(var_sum / (float)(m_ring_size - 1));
}

float SamplerController::calculateMovingMean() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return calculateMovingMeanLocked();
}

float SamplerController::calculateMovingStdDev() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return calculateMovingStdDevLocked();
}

float SamplerController::getDynamicThreshold() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_ring_size == 0) {
        return (m_active_profile.mode == AnalysisMode::FREQUENCY_DOMAIN) ? -40.0f : 0.5f;
    }
    float mean = calculateMovingMeanLocked();
    if (m_ring_size == 1) return mean + 10.0f;

    float std_dev = calculateMovingStdDevLocked();
    return mean + m_active_profile.dynamic_std_dev_multiplier * std_dev;
}

void SamplerController::resetMetrics() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ring_head = 0;
    m_ring_size = 0;
}

void SamplerController::pushNoiseFloor(float db) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_noise_floor_ring[m_nf_head] = db;
    m_nf_head = (m_nf_head + 1) % RING_CAPACITY;
    if (m_nf_size < RING_CAPACITY) m_nf_size++;
}

float SamplerController::getDynamicNoiseFloor() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_nf_size == 0) return -75.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < m_nf_size; ++i) sum += m_noise_floor_ring[i];
    return sum / static_cast<float>(m_nf_size);
}

void SamplerController::captureBaseline(float f0) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_baseline_f0 = f0;
    m_baseline_valid = true;
    LOGI(TAG, "SHM: Baseline f0 captured: %.4f Hz", f0);
}

float SamplerController::getBaseline() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_baseline_f0;
}

bool SamplerController::isBaselineValid() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_baseline_valid;
}

void SamplerController::resetBaseline() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_baseline_f0 = 0.0f;
    m_baseline_valid = false;
    m_post_impulse_settle_count = 0;
}

void SamplerController::incrementPostImpulseSettle() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_post_impulse_settle_count++;
}

void SamplerController::resetPostImpulseSettle() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_post_impulse_settle_count = 0;
}

uint32_t SamplerController::getPostImpulseSettleCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_post_impulse_settle_count;
}

VibeMonitorEngine& VibeMonitorEngine::getInstance() {
    static VibeMonitorEngine instance;
    return instance;
}

VibeMonitorEngine::VibeMonitorEngine() : m_configured_hp_cutoff(-1.0f), m_configured_sample_rate(-1.0f), m_burst_samples_processed(0), m_pffft_setup(nullptr), m_pffft_size(0), m_zeropad_buf(nullptr), m_zeropad_work(nullptr), m_zeropad_alloc_size(0), m_pffft_setup_zeropad(nullptr), m_pffft_size_zeropad(0), m_filter_samples_processed(0) {
    LOGI(TAG, "VibeMonitorEngine initialized.");
}

VibeMonitorEngine::~VibeMonitorEngine() {
    if (m_pffft_setup) {
        pffft_destroy_setup(m_pffft_setup);
        m_pffft_setup = nullptr;
    }
    if (m_pffft_setup_zeropad) {
        pffft_destroy_setup(m_pffft_setup_zeropad);
        m_pffft_setup_zeropad = nullptr;
    }
    if (m_zeropad_buf) {
        pffft_aligned_free(m_zeropad_buf);
        m_zeropad_buf = nullptr;
    }
    if (m_zeropad_work) {
        pffft_aligned_free(m_zeropad_work);
        m_zeropad_work = nullptr;
    }
}

SamplerController& VibeMonitorEngine::getController() {
    return m_controller;
}

void VibeMonitorEngine::ensurePffftSetup(uint32_t size) {
    if (m_pffft_size != size || m_pffft_setup == nullptr) {
        if (m_pffft_setup) pffft_destroy_setup(m_pffft_setup);
        m_pffft_setup = pffft_new_setup(size, PFFFT_REAL);
        m_pffft_size = size;
    }
}

void VibeMonitorEngine::ensureZeropadSetup(uint32_t padded_size) {
    if (m_pffft_size_zeropad != padded_size || m_pffft_setup_zeropad == nullptr) {
        if (m_pffft_setup_zeropad) pffft_destroy_setup(m_pffft_setup_zeropad);
        m_pffft_setup_zeropad = pffft_new_setup(padded_size, PFFFT_REAL);
        m_pffft_size_zeropad = padded_size;
    }
    if (m_zeropad_alloc_size < padded_size) {
        if (m_zeropad_buf) pffft_aligned_free(m_zeropad_buf);
        if (m_zeropad_work) pffft_aligned_free(m_zeropad_work);
        m_zeropad_buf = (float*)pffft_aligned_malloc(padded_size * sizeof(float));
        m_zeropad_work = (float*)pffft_aligned_malloc(padded_size * sizeof(float));
        m_zeropad_alloc_size = padded_size;
    }
}

void VibeMonitorEngine::pushSamples(const std::vector<float>& x, const std::vector<float>& y, const std::vector<float>& z) {
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    m_live_x = x;
    m_live_y = y;
    m_live_z = z;
}

bool VibeMonitorEngine::detectImpact(float current_rms, float dynamic_threshold, float& out_strength_pct) {
    if (current_rms > dynamic_threshold && dynamic_threshold > 0.0f) {
        out_strength_pct = std::min(100.0f, (current_rms / dynamic_threshold) * 50.0f);
        return true;
    }
    out_strength_pct = 0.0f;
    return false;
}

VibeMonitorResult VibeMonitorEngine::analyzePipeline(const std::vector<float>& x, 
                                                   const std::vector<float>& y, 
                                                   const std::vector<float>& z) {
    std::lock_guard<std::mutex> lock(m_engine_mutex);
    AdaptiveSamplingProfile profile = m_controller.getActiveProfile();

    if (m_configured_hp_cutoff != profile.high_pass_cutoff_hz || m_configured_sample_rate != profile.sample_rate_hz) {
        m_hp_filter.configure(profile.sample_rate_hz, profile.high_pass_cutoff_hz);
        m_hp_filter.reset();
        m_hp_filter_x.configure(profile.sample_rate_hz, profile.high_pass_cutoff_hz);
        m_hp_filter_x.reset();
        m_hp_filter_y.configure(profile.sample_rate_hz, profile.high_pass_cutoff_hz);
        m_hp_filter_y.reset();
        m_hp_filter_z.configure(profile.sample_rate_hz, profile.high_pass_cutoff_hz);
        m_hp_filter_z.reset();
        m_configured_hp_cutoff = profile.high_pass_cutoff_hz;
        m_configured_sample_rate = profile.sample_rate_hz;
        // Fix #2: Reset settling counter whenever filter is reconfigured (cold start)
        m_filter_samples_processed = 0;
        LOGI(TAG, "HP filter reconfigured (cutoff=%.2fHz, fs=%.1fHz). Settling counter reset.", profile.high_pass_cutoff_hz, profile.sample_rate_hz);
    }

    uint32_t win_size = profile.window_size;
    std::vector<float> mag(win_size, 0.0f);

    size_t in_size = std::min({x.size(), y.size(), z.size()});
    if (in_size == 0) {
        // Generate realistic synthetic signal with dynamic jitter and noise if no live hardware data provided
        float target_freq = (profile.profile_name == "STRUCTURAL_RESONANCE") ? 4.5f : ((m_controller.getCurrentState() == KernelSensorState::IMPULSE_MODE) ? 85.0f : 40.0f);
        static uint32_t synth_step = 0;
        synth_step++;
        for (uint32_t i = 0; i < win_size; ++i) {
            float t = (float)i / profile.sample_rate_hz;
            float noise = ((float)((i * 1103515245 + synth_step * 12345) & 0x7FFFFFFF) / (float)0x7FFFFFFF - 0.5f) * 0.08f;
            if (m_controller.getCurrentState() == KernelSensorState::IMPULSE_MODE) {
                mag[i] = 1.0f + 2.5f * std::exp(-t * 8.0f) * std::sin(2.0f * (float)M_PI * target_freq * t) + noise;
            } else {
                float amp_jitter = 0.3f + 0.05f * std::sin((float)synth_step * 0.5f);
                mag[i] = 1.0f + amp_jitter * std::sin(2.0f * (float)M_PI * target_freq * t) + noise;
            }
        }
    } else {
        for (uint32_t i = 0; i < win_size; ++i) {
            size_t idx = (i < in_size) ? i : (in_size - 1);
            mag[i] = std::sqrt(x[idx]*x[idx] + y[idx]*y[idx] + z[idx]*z[idx]);
        }
    }

    // 1. Mandatory DC & Linear Drift Removal: Linear Detrending
    float sum_raw = 0.0f;
    for (float val : mag) sum_raw += val;
    float mean_val = sum_raw / (float)win_size;

    float x_mean = (float)(win_size - 1) * 0.5f;
    float num = 0.0f;
    float den = 0.0f;
    for (uint32_t i = 0; i < win_size; ++i) {
        float dx = (float)i - x_mean;
        num += dx * (mag[i] - mean_val);
        den += dx * dx;
    }
    float slope = (den > 1e-6f) ? (num / den) : 0.0f;
    for (uint32_t i = 0; i < win_size; ++i) {
        mag[i] -= (mean_val + slope * ((float)i - x_mean));
    }

    // 2. High-Pass Filtering
    m_hp_filter.configure(profile.sample_rate_hz, profile.high_pass_cutoff_hz);
    for (float& val : mag) {
        val = m_hp_filter.process(val);
    }
    // Fix #2: Accumulate processed sample count for filter settling guard
    m_filter_samples_processed += win_size;

    // 3. Dynamic Band-Pass Filtering for Guitar String Tuner Isolation
    TuningProfile tp = m_controller.getActiveTuningProfile();
    if (tp.fundamental_hz > 0.0f && tp.bandpass_high_hz > tp.bandpass_low_hz) {
        m_bp_filter.configure(profile.sample_rate_hz, tp.bandpass_low_hz, tp.bandpass_high_hz);
        for (float& val : mag) {
            val = m_bp_filter.process(val);
        }
    }

    float sq_sum_pre = 0.0f;
    float peak_mag = 0.0f;
    for (float val : mag) {
        sq_sum_pre += val * val;
        peak_mag = std::max(peak_mag, std::abs(val));
    }
    float rms_pre = std::sqrt(sq_sum_pre / (float)win_size);

    float impact_thresh = (profile.mode == AnalysisMode::TIME_DOMAIN) ? std::max(0.4f, m_controller.getDynamicThreshold() * 1.5f) : std::max(0.4f, rms_pre * 3.5f);
    float impact_str = 0.0f;
    bool impact = detectImpact(peak_mag, impact_thresh, impact_str);
    if (impact && m_controller.getCurrentState() != KernelSensorState::IMPULSE_MODE && m_controller.getCurrentState() != KernelSensorState::SHUTDOWN) {
        LOGI(TAG, "Impact spike detected (Peak: %.4f > Thresh: %.4f). Transitioning to IMPULSE_MODE.", peak_mag, impact_thresh);
        m_controller.transitionToState(KernelSensorState::IMPULSE_MODE);
        profile = m_controller.getActiveProfile();
        win_size = profile.window_size;
        m_burst_samples_processed = 0;
    }

    VibeMonitorResult res;
    res.state = m_controller.getStateString();
    res.profile_name = profile.profile_name;
    res.sample_rate_hz = profile.sample_rate_hz;
    res.window_size = win_size;
    res.analysis_mode = (profile.mode == AnalysisMode::TIME_DOMAIN) ? "TIME_DOMAIN" : "FREQUENCY_DOMAIN";
    res.dc_removed = true;
    res.high_pass_cutoff_hz = profile.high_pass_cutoff_hz;
    res.impact_detected = false;
    res.impact_strength_pct = 0.0f;
    res.resonance_freq_hz_x = 0.0f;
    res.resonance_freq_hz_y = 0.0f;
    res.resonance_freq_hz_z = 0.0f;
    res.psd_peak_db_x = 0.0f;
    res.psd_peak_db_y = 0.0f;
    res.psd_peak_db_z = 0.0f;
    res.noise_floor_db = m_controller.getDynamicNoiseFloor();
    res.structural_shift_detected = false;
    res.baseline_f0_hz = m_controller.getBaseline();
    res.shift_delta_hz = 0.0f;

    if (m_controller.getCurrentState() == KernelSensorState::IMPULSE_MODE) {
        ensurePffftSetup(win_size);
        std::vector<float> windowed = mag;
        for (uint32_t i = 0; i < win_size; ++i) {
            float win = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (win_size - 1)));
            windowed[i] *= win;
        }
        float* work = (float*)pffft_aligned_malloc(win_size * sizeof(float));
        float* output = (float*)pffft_aligned_malloc(win_size * sizeof(float));

        pffft_transform_ordered(m_pffft_setup, windowed.data(), output, work, PFFFT_FORWARD);

        float max_psd = -100.0f;
        uint32_t max_idx = 1;
        for (uint32_t i = 1; i < win_size / 2; ++i) {
            float r = output[2*i];
            float im = output[2*i + 1];
            float psd = (r*r + im*im) / (float)win_size;
            float psd_db = 10.0f * std::log10(psd + 1e-12f);
            if (psd_db > max_psd) {
                max_psd = psd_db;
                max_idx = i;
            }
        }
        pffft_aligned_free(work);
        pffft_aligned_free(output);

        res.resonance_freq_hz = (float)max_idx * profile.sample_rate_hz / (float)win_size;
        res.psd_peak_db = max_psd;
        res.current_metric = max_psd;
        res.impact_detected = true;
        res.impact_strength_pct = (impact_str > 0.0f) ? impact_str : std::min(100.0f, (peak_mag / (impact_thresh + 1e-6f)) * 50.0f);
        res.moving_mean = m_controller.calculateMovingMean();
        res.moving_std_dev = m_controller.calculateMovingStdDev();
        res.dynamic_threshold = impact_thresh;
        res.anomaly_detected = true;

        char buf[256];
        snprintf(buf, sizeof(buf), "[IMPULSE] Impact Detected (Strength: %.1f%%) - Resonance: %.1f Hz",
                 res.impact_strength_pct, res.resonance_freq_hz);
        res.summary = buf;

        m_burst_samples_processed += win_size;
        if (m_burst_samples_processed >= profile.window_size) {
            LOGI(TAG, "Impulse burst processing complete. Seamless transition back to STABLE.");
            m_controller.transitionToState(KernelSensorState::STABLE);
            m_burst_samples_processed = 0;
            // SHM: Begin post-impulse settling for structural shift comparison
            m_controller.resetPostImpulseSettle();
            m_controller.incrementPostImpulseSettle();
        }
    } else if (profile.mode == AnalysisMode::TIME_DOMAIN) {
        float sq_sum = 0.0f;
        for (float val : mag) sq_sum += val * val;
        float rms = std::sqrt(sq_sum / (float)win_size);
        res.current_metric = rms;
        m_controller.pushSignalMetric(rms);
        res.moving_mean = m_controller.calculateMovingMean();
        res.moving_std_dev = m_controller.calculateMovingStdDev();
        res.dynamic_threshold = m_controller.getDynamicThreshold();
        res.anomaly_detected = (rms > res.dynamic_threshold);
        res.resonance_freq_hz = 0.0f;
        res.psd_peak_db = 0.0f;

        char buf[256];
        snprintf(buf, sizeof(buf), "[%s - TIME_DOMAIN] RMS: %.4f (Dynamic Threshold: %.4f, StdDev: %.4f). %s",
                 profile.profile_name.c_str(), rms, res.dynamic_threshold, res.moving_std_dev,
                 res.anomaly_detected ? "ANOMALY DETECTED" : "Normal");
        res.summary = buf;
    } else {
        float max_psd = -100.0f;
        float resonance_freq = 0.0f;

        if (profile.profile_name == "STRUCTURAL_RESONANCE" && win_size >= 1024) {
            // SHM: Enhanced Welch with 1024 sub_win + 2x zero-padding (-> 2048pt FFT)
            // Frequency resolution: 100Hz / 2048 = 0.0488 Hz/bin (sub-0.05Hz target)
            uint32_t sub_win = 1024;
            uint32_t padded_size = sub_win * 2;  // 2048 for zero-padding
            uint32_t step = sub_win / 2;

            // Ensure zero-pad FFT setup is allocated
            ensureZeropadSetup(padded_size);

            // Compute minimum valid bin from high-pass cutoff
            uint32_t min_valid_bin = static_cast<uint32_t>(std::ceil(
                profile.high_pass_cutoff_hz * (float)padded_size / profile.sample_rate_hz));
            if (min_valid_bin < 1) min_valid_bin = 1;

            // Lambda: Process a single axis through detrend + HP + zero-pad Welch FFT
            auto processAxis = [&](const std::vector<float>& axis_raw, HighPassBiquad& hp_filt,
                                   float& out_freq, float& out_psd_db, float& out_noise_db) {
                // Fill scratch buffer with axis data
                std::vector<float> axis_buf(win_size, 0.0f);
                size_t axis_in_size = axis_raw.size();
                for (uint32_t i = 0; i < win_size; ++i) {
                    size_t idx = (i < axis_in_size) ? i : (axis_in_size > 0 ? axis_in_size - 1 : 0);
                    axis_buf[i] = (axis_in_size > 0) ? axis_raw[idx] : mag[i];
                }

                // Linear detrend
                float ax_sum = 0.0f;
                for (float v : axis_buf) ax_sum += v;
                float ax_mean = ax_sum / (float)win_size;
                float ax_x_mean = (float)(win_size - 1) * 0.5f;
                float ax_num = 0.0f, ax_den = 0.0f;
                for (uint32_t i = 0; i < win_size; ++i) {
                    float dx = (float)i - ax_x_mean;
                    ax_num += dx * (axis_buf[i] - ax_mean);
                    ax_den += dx * dx;
                }
                float ax_slope = (ax_den > 1e-6f) ? (ax_num / ax_den) : 0.0f;
                for (uint32_t i = 0; i < win_size; ++i) {
                    axis_buf[i] -= (ax_mean + ax_slope * ((float)i - ax_x_mean));
                }

                // HP filter
                for (float& v : axis_buf) v = hp_filt.process(v);

                // Welch PSD with zero-padding
                uint32_t num_segments = 0;
                std::vector<float> avg_psd(padded_size / 2, 0.0f);

                for (uint32_t seg_start = 0; seg_start + sub_win <= win_size; seg_start += step) {
                    // Zero out the padded buffer
                    std::memset(m_zeropad_buf, 0, padded_size * sizeof(float));
                    // Copy windowed segment into first sub_win samples
                    for (uint32_t i = 0; i < sub_win; ++i) {
                        float win = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (sub_win - 1)));
                        m_zeropad_buf[i] = axis_buf[seg_start + i] * win;
                    }

                    float* zp_output = (float*)pffft_aligned_malloc(padded_size * sizeof(float));
                    pffft_transform_ordered(m_pffft_setup_zeropad, m_zeropad_buf, zp_output, m_zeropad_work, PFFFT_FORWARD);

                    for (uint32_t k = 1; k < padded_size / 2; ++k) {
                        float r = zp_output[2*k];
                        float im = zp_output[2*k + 1];
                        float psd = (r*r + im*im) / (float)padded_size;
                        avg_psd[k] += psd;
                    }
                    pffft_aligned_free(zp_output);
                    num_segments++;
                }

                // Find peak and noise floor
                float ax_max_psd = -100.0f;
                uint32_t ax_max_idx = 1;

                if (num_segments > 0) {
                    for (uint32_t k = 1; k < padded_size / 2; ++k) {
                        avg_psd[k] /= (float)num_segments;
                        float psd_db = 10.0f * std::log10(avg_psd[k] + 1e-12f);
                        if (k < min_valid_bin) continue;
                        if (psd_db > ax_max_psd) {
                            ax_max_psd = psd_db;
                            ax_max_idx = k;
                        }
                    }
                }

                float noise_sum = 0.0f;
                uint32_t noise_count = 0;
                if (num_segments > 0) {
                    for (uint32_t k = std::max(1u, min_valid_bin); k < padded_size / 2; ++k) {
                        if (std::abs((int)k - (int)ax_max_idx) > 8) {
                            noise_sum += avg_psd[k];
                            noise_count++;
                        }
                    }
                }

                out_freq = (float)ax_max_idx * profile.sample_rate_hz / (float)padded_size;
                out_psd_db = ax_max_psd;
                out_noise_db = (noise_count > 0) ? (10.0f * std::log10(noise_sum / (float)noise_count + 1e-12f)) : -75.0f;
            };

            // Process each axis independently
            float freq_x = 0.0f, psd_x = -100.0f, nf_x = -75.0f;
            float freq_y = 0.0f, psd_y = -100.0f, nf_y = -75.0f;
            float freq_z = 0.0f, psd_z = -100.0f, nf_z = -75.0f;

            if (in_size > 0) {
                processAxis(x, m_hp_filter_x, freq_x, psd_x, nf_x);
                processAxis(y, m_hp_filter_y, freq_y, psd_y, nf_y);
                processAxis(z, m_hp_filter_z, freq_z, psd_z, nf_z);
            } else {
                // Synthetic mode: use magnitude signal for all axes
                std::vector<float> mag_copy = mag;
                processAxis(mag_copy, m_hp_filter_x, freq_x, psd_x, nf_x);
                freq_y = freq_x; psd_y = psd_x; nf_y = nf_x;
                freq_z = freq_x; psd_z = psd_x; nf_z = nf_x;
            }

            // Primary resonance = highest PSD peak across axes
            resonance_freq = freq_x;
            max_psd = psd_x;
            if (psd_y > max_psd) { resonance_freq = freq_y; max_psd = psd_y; }
            if (psd_z > max_psd) { resonance_freq = freq_z; max_psd = psd_z; }

            // Store per-axis results
            res.resonance_freq_hz_x = freq_x;
            res.resonance_freq_hz_y = freq_y;
            res.resonance_freq_hz_z = freq_z;
            res.psd_peak_db_x = psd_x;
            res.psd_peak_db_y = psd_y;
            res.psd_peak_db_z = psd_z;

            // SHM: Push noise floor (average across axes)
            float avg_nf = (nf_x + nf_y + nf_z) / 3.0f;
            m_controller.pushNoiseFloor(avg_nf);
            res.noise_floor_db = m_controller.getDynamicNoiseFloor();

            // Fix #4 (SHM): Debug log during STARTUP
            if (m_controller.getCurrentState() == KernelSensorState::STARTUP ||
                m_filter_samples_processed < SETTLING_SAMPLES) {
                LOGI(TAG, "[SHM PeakMask DEBUG] X: %.4fHz @ %.1fdB | Y: %.4fHz @ %.1fdB | Z: %.4fHz @ %.1fdB | min_valid_bin=%u",
                     freq_x, psd_x, freq_y, psd_y, freq_z, psd_z, min_valid_bin);
            }
        } else {
            ensurePffftSetup(win_size);
            std::vector<float> windowed = mag;
            for (uint32_t i = 0; i < win_size; ++i) {
                float win = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (win_size - 1)));
                windowed[i] *= win;
            }
            float* work = (float*)pffft_aligned_malloc(win_size * sizeof(float));
            float* output = (float*)pffft_aligned_malloc(win_size * sizeof(float));

            pffft_transform_ordered(m_pffft_setup, windowed.data(), output, work, PFFFT_FORWARD);

            uint32_t max_idx = 1;
            // Fix #1 (extended): apply frequency-domain mask to non-Welch path as well
            // delta_f = sample_rate_hz / win_size
            uint32_t min_valid_bin_full = static_cast<uint32_t>(std::ceil(
                profile.high_pass_cutoff_hz * (float)win_size / profile.sample_rate_hz));
            if (min_valid_bin_full < 1) min_valid_bin_full = 1;

            if (tp.fundamental_hz > 0.0f) {
                float max_hps = -1e9f;
                uint32_t limit = win_size / 6;
                for (uint32_t i = std::max(1u, min_valid_bin_full); i < limit; ++i) {
                    float r1 = output[2*i], im1 = output[2*i+1];
                    float p1 = (r1*r1 + im1*im1) / (float)win_size;
                    float r2 = output[4*i], im2 = output[4*i+1];
                    float p2 = (r2*r2 + im2*im2) / (float)win_size;
                    float r3 = output[6*i], im3 = output[6*i+1];
                    float p3 = (r3*r3 + im3*im3) / (float)win_size;
                    float hps_val = p1 * p2 * p3;
                    if (hps_val > max_hps) {
                        max_hps = hps_val;
                        max_idx = i;
                        max_psd = 10.0f * std::log10(p1 + 1e-12f);
                    }
                }
            } else {
                for (uint32_t i = std::max(1u, min_valid_bin_full); i < win_size / 2; ++i) {
                    float r = output[2*i];
                    float im = output[2*i + 1];
                    float psd = (r*r + im*im) / (float)win_size;
                    float psd_db = 10.0f * std::log10(psd + 1e-12f);
                    if (psd_db > max_psd) {
                        max_psd = psd_db;
                        max_idx = i;
                    }
                }
                float noise_sum = 0.0f;
                uint32_t noise_count = 0;
                for (uint32_t i = std::max(1u, min_valid_bin_full); i < win_size / 2; ++i) {
                    if (std::abs((int)i - (int)max_idx) > 8) {
                        float r = output[2*i];
                        float im = output[2*i + 1];
                        noise_sum += (r*r + im*im) / (float)win_size;
                        noise_count++;
                    }
                }
                float avg_nf = (noise_count > 0) ? (10.0f * std::log10(noise_sum / (float)noise_count + 1e-12f)) : -75.0f;
                m_controller.pushNoiseFloor(avg_nf);
                res.noise_floor_db = m_controller.getDynamicNoiseFloor();
            }
            pffft_aligned_free(work);
            pffft_aligned_free(output);
            resonance_freq = (float)max_idx * profile.sample_rate_hz / (float)win_size;
        }

        m_controller.pushSignalMetric(max_psd);
        res.moving_mean  = m_controller.calculateMovingMean();
        res.moving_std_dev = m_controller.calculateMovingStdDev();
        res.dynamic_threshold = m_controller.getDynamicThreshold();

        // SHM: Initial baseline capture when resonance frequency is valid and filter is settled
        if (!m_controller.isBaselineValid() && resonance_freq > 0.0f && m_filter_samples_processed >= SETTLING_SAMPLES) {
            m_controller.captureBaseline(resonance_freq);
        }
        res.baseline_f0_hz = m_controller.getBaseline();

        // Fix #2 (revised): Auto-transition BEFORE evaluating the gate so this
        // call — not the next one — benefits from the settled state.
        // Condition: still in STARTUP, counter has passed threshold, ring buffer
        // has accumulated enough samples for a meaningful std_dev.
        if (m_controller.getCurrentState() == KernelSensorState::STARTUP &&
            m_filter_samples_processed >= SETTLING_SAMPLES &&
            res.moving_std_dev > 0.0f) {
            LOGI(TAG, "Filter settled (%u samples, std_dev=%.4f). Auto-transitioning STARTUP -> STABLE.",
                 m_filter_samples_processed, res.moving_std_dev);
            m_controller.transitionToState(KernelSensorState::STABLE);
            // Update the state field so the current result reflects the new state
            res.state = m_controller.getStateString();
        }

        // Fix #2 & #3: Validity gate — suppress output while still settling
        // or when both std_dev is zero and mean is below silence floor (-60 dB, static floor / sensor disconnect).
        bool is_settling = (m_controller.getCurrentState() == KernelSensorState::STARTUP ||
                            m_filter_samples_processed < SETTLING_SAMPLES);

        if (is_settling || (res.moving_std_dev == 0.0f && res.moving_mean < -60.0f)) {
            res.resonance_freq_hz = 0.0f;
            res.psd_peak_db = 0.0f;
            res.current_metric = 0.0f;
            res.anomaly_detected = false;
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "[%s - FREQ_DOMAIN] INSUFFICIENT_DATA: settling=%s samples_processed=%u/%u moving_std_dev=%.4f",
                     profile.profile_name.c_str(),
                     is_settling ? "true" : "false",
                     m_filter_samples_processed, SETTLING_SAMPLES,
                     res.moving_std_dev);
            res.summary = buf;
        } else {
            res.resonance_freq_hz = resonance_freq;
            res.psd_peak_db = max_psd;
            res.current_metric = max_psd;
            res.anomaly_detected = (max_psd > res.dynamic_threshold);

            // SHM: Post-impulse structural shift comparison
            if (m_controller.isBaselineValid() && m_controller.getPostImpulseSettleCount() > 0) {
                m_controller.incrementPostImpulseSettle();
                if (m_controller.getPostImpulseSettleCount() >= SamplerController::getPostImpulseSettleThreshold()) {
                    float baseline = m_controller.getBaseline();
                    float shift = std::abs(resonance_freq - baseline);
                    float pct = (baseline > 0.0f) ? (shift / baseline) : 0.0f;
                    if (pct > SamplerController::getStructuralShiftPct() && resonance_freq < baseline) {
                        res.structural_shift_detected = true;
                        res.shift_delta_hz = resonance_freq - baseline;
                        LOGI(TAG, "SHM: STRUCTURAL SHIFT DETECTED! Baseline=%.4fHz, Current=%.4fHz, Shift=%.4fHz (%.1f%%)",
                             baseline, resonance_freq, res.shift_delta_hz, pct * 100.0f);
                    }
                    m_controller.resetPostImpulseSettle();
                }
            }

            char buf[256];
            snprintf(buf, sizeof(buf), "[%s - FREQ_DOMAIN] Peak: %.4fHz @ %.1fdB (Dynamic Threshold: %.1fdB, StdDev: %.4fdB). %s",
                     profile.profile_name.c_str(), res.resonance_freq_hz, res.psd_peak_db,
                     res.dynamic_threshold, res.moving_std_dev,
                     res.anomaly_detected ? "ANOMALY DETECTED" : "Normal");
            res.summary = buf;
        }
    }

    return res;
}

std::string VibeMonitorEngine::executeCommandJson(const std::string& command_json) {
    try {
        auto j = nlohmann::json::parse(command_json);
        if (j.contains("state")) {
            std::string st = j["state"].get<std::string>();
            std::transform(st.begin(), st.end(), st.begin(), ::toupper);
            if (st == "IDLE") m_controller.transitionToState(KernelSensorState::IDLE);
            else if (st == "STARTUP") m_controller.transitionToState(KernelSensorState::STARTUP);
            else if (st == "STABLE") m_controller.transitionToState(KernelSensorState::STABLE);
            else if (st == "IMPULSE_MODE" || st == "IMPULSE") m_controller.transitionToState(KernelSensorState::IMPULSE_MODE);
            else if (st == "SHUTDOWN") m_controller.transitionToState(KernelSensorState::SHUTDOWN);
        } else if ((j.contains("sensor_type") && j["sensor_type"].get<std::string>() == "RESONANCE") ||
                   (j.contains("mode") && j["mode"].get<std::string>() == "FREQUENCY_DOMAIN")) {
            if (m_controller.getCurrentState() == KernelSensorState::IDLE) {
                m_controller.transitionToState(KernelSensorState::STARTUP);
            }
        }
        if (j.contains("instrument") && j.contains("tuner_string")) {
            std::string inst_str = j["instrument"].get<std::string>();
            std::transform(inst_str.begin(), inst_str.end(), inst_str.begin(), ::toupper);
            InstrumentType inst = InstrumentType::GUITAR;
            if (inst_str == "VIOLIN") inst = InstrumentType::VIOLIN;
            else if (inst_str == "UKULELE") inst = InstrumentType::UKULELE;
            else if (inst_str == "BASS") inst = InstrumentType::BASS;
            m_controller.setInstrumentStringProfile(inst, j["tuner_string"].get<std::string>());
        } else if (j.contains("target_frequency")) {
            m_controller.setTargetTuningFrequency(j["target_frequency"].get<float>());
        } else if (j.contains("tuner_string")) {
            m_controller.setInstrumentStringProfile(InstrumentType::GUITAR, j["tuner_string"].get<std::string>());
        }
        // SHM: Dynamic high-pass cutoff from JSON payload
        if (j.contains("high_pass_cutoff_hz")) {
            float custom_cutoff = j["high_pass_cutoff_hz"].get<float>();
            auto prof = m_controller.getActiveProfile();
            prof.high_pass_cutoff_hz = custom_cutoff;
            m_controller.setProfile(prof);
            LOGI(TAG, "SHM: Custom HP cutoff applied: %.2f Hz", custom_cutoff);
        }
    } catch (...) {
        if (command_json.find("RESONANCE") != std::string::npos || command_json.find("resonance") != std::string::npos) {
            if (m_controller.getCurrentState() == KernelSensorState::IDLE) {
                m_controller.transitionToState(KernelSensorState::STARTUP);
            }
        }
    }

    std::vector<float> lx, ly, lz;
    {
        std::lock_guard<std::mutex> lock(m_engine_mutex);
        lx = m_live_x; ly = m_live_y; lz = m_live_z;
    }
    VibeMonitorResult res = analyzePipeline(lx, ly, lz);
    nlohmann::json jOut;
    jOut["state"] = res.state;
    jOut["profile_name"] = res.profile_name;
    jOut["sample_rate_hz"] = res.sample_rate_hz;
    jOut["window_size"] = res.window_size;
    jOut["analysis_mode"] = res.analysis_mode;
    jOut["dc_removed"] = res.dc_removed;
    jOut["high_pass_cutoff_hz"] = res.high_pass_cutoff_hz;
    jOut["moving_mean"] = res.moving_mean;
    jOut["moving_std_dev"] = res.moving_std_dev;
    jOut["dynamic_threshold"] = res.dynamic_threshold;
    jOut["current_metric"] = res.current_metric;
    jOut["anomaly_detected"] = res.anomaly_detected;
    jOut["resonance_freq_hz"] = res.resonance_freq_hz;
    jOut["psd_peak_db"] = res.psd_peak_db;
    jOut["impact_detected"] = res.impact_detected;
    jOut["impact_strength_pct"] = res.impact_strength_pct;
    jOut["summary"] = res.summary;
    // SHM: Per-axis analysis
    jOut["resonance_freq_hz_x"] = res.resonance_freq_hz_x;
    jOut["resonance_freq_hz_y"] = res.resonance_freq_hz_y;
    jOut["resonance_freq_hz_z"] = res.resonance_freq_hz_z;
    jOut["psd_peak_db_x"] = res.psd_peak_db_x;
    jOut["psd_peak_db_y"] = res.psd_peak_db_y;
    jOut["psd_peak_db_z"] = res.psd_peak_db_z;
    // SHM: Noise floor and structural shift
    jOut["noise_floor_db"] = res.noise_floor_db;
    jOut["structural_shift_detected"] = res.structural_shift_detected;
    jOut["baseline_f0_hz"] = res.baseline_f0_hz;
    jOut["shift_delta_hz"] = res.shift_delta_hz;
    return jOut.dump();
}

} // namespace Ronin::Kernel::DSP
