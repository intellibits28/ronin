#include "dsp/vibe_monitor.h"
#include <algorithm>
#include <sstream>
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
    if (sample_rate_hz <= 0.0f || cutoff_hz <= 0.0f || cutoff_hz >= sample_rate_hz * 0.5f) {
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
        return;
    }
    float w0 = 2.0f * (float)M_PI * cutoff_hz / sample_rate_hz;
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
    m_tuning_profile = {"NONE", 0.0f, 0.0f, 0.0f};
    transitionToState(KernelSensorState::IDLE);
}

void SamplerController::setTargetTuningFrequency(float target_hz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (target_hz <= 0.0f) {
        m_tuning_profile = {"NONE", 0.0f, 0.0f, 0.0f};
        return;
    }
    std::string name = "CUSTOM";
    if (std::abs(target_hz - 329.63f) < 5.0f) name = "E4";
    else if (std::abs(target_hz - 246.94f) < 5.0f) name = "B3";
    else if (std::abs(target_hz - 196.00f) < 5.0f) name = "G3";
    else if (std::abs(target_hz - 146.83f) < 5.0f) name = "D3";
    else if (std::abs(target_hz - 110.00f) < 5.0f) name = "A2";
    else if (std::abs(target_hz - 82.41f) < 5.0f) name = "E2";

    float low_hz = target_hz * 0.91f;
    float high_hz = target_hz * 1.21f;
    if (name == "E4") { low_hz = 300.0f; high_hz = 400.0f; }

    m_tuning_profile = {name, target_hz, low_hz, high_hz};
    float new_rate = std::max(2000.0f, target_hz * 4.5f);
    m_active_profile.sample_rate_hz = new_rate;
    LOGI(TAG, "Dynamic Tuning Profile configured for %s (Target: %.2fHz, Bandpass: %.1f-%.1fHz, Rate: %.1fHz)",
         name.c_str(), target_hz, low_hz, high_hz, new_rate);
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
            // Multi-Resolution Support: High-frequency machine diagnostics (e.g. 1024 samples at 200Hz for Motor/Compressor)
            m_active_profile = {"MACHINE_DIAGNOSTICS", 200.0f, 1024, AnalysisMode::FREQUENCY_DOMAIN, 0.5f, 3.0f};
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

VibeMonitorEngine& VibeMonitorEngine::getInstance() {
    static VibeMonitorEngine instance;
    return instance;
}

VibeMonitorEngine::VibeMonitorEngine() : m_configured_hp_cutoff(-1.0f), m_configured_sample_rate(-1.0f), m_burst_samples_processed(0), m_pffft_setup(nullptr), m_pffft_size(0) {
    LOGI(TAG, "VibeMonitorEngine initialized.");
}

VibeMonitorEngine::~VibeMonitorEngine() {
    if (m_pffft_setup) {
        pffft_destroy_setup(m_pffft_setup);
        m_pffft_setup = nullptr;
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
        m_configured_hp_cutoff = profile.high_pass_cutoff_hz;
        m_configured_sample_rate = profile.sample_rate_hz;
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
            // Welch's Method: average PSD across multiple overlapping 2.56s (256 samples) Hanning windows
            uint32_t sub_win = 256;
            uint32_t step = sub_win / 2; // 50% overlap
            uint32_t num_segments = 0;
            std::vector<float> avg_psd(sub_win / 2, 0.0f);

            ensurePffftSetup(sub_win);
            float* work = (float*)pffft_aligned_malloc(sub_win * sizeof(float));
            float* output = (float*)pffft_aligned_malloc(sub_win * sizeof(float));

            for (uint32_t start = 0; start + sub_win <= win_size; start += step) {
                std::vector<float> segment(sub_win);
                for (uint32_t i = 0; i < sub_win; ++i) {
                    float win = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (sub_win - 1)));
                    segment[i] = mag[start + i] * win;
                }
                pffft_transform_ordered(m_pffft_setup, segment.data(), output, work, PFFFT_FORWARD);

                for (uint32_t k = 1; k < sub_win / 2; ++k) {
                    float r = output[2*k];
                    float im = output[2*k + 1];
                    float psd = (r*r + im*im) / (float)sub_win;
                    avg_psd[k] += psd;
                }
                num_segments++;
            }
            pffft_aligned_free(work);
            pffft_aligned_free(output);

            uint32_t max_idx = 1;
            if (num_segments > 0) {
                for (uint32_t k = 1; k < sub_win / 2; ++k) {
                    avg_psd[k] /= (float)num_segments;
                    float psd_db = 10.0f * std::log10(avg_psd[k] + 1e-12f);
                    if (psd_db > max_psd) {
                        max_psd = psd_db;
                        max_idx = k;
                    }
                }
            }
            resonance_freq = (float)max_idx * profile.sample_rate_hz / (float)sub_win;
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
            if (tp.fundamental_hz > 0.0f) {
                float max_hps = -1e9f;
                uint32_t limit = win_size / 6;
                for (uint32_t i = 1; i < limit; ++i) {
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
            }
            pffft_aligned_free(work);
            pffft_aligned_free(output);
            resonance_freq = (float)max_idx * profile.sample_rate_hz / (float)win_size;
        }

        res.resonance_freq_hz = resonance_freq;
        res.psd_peak_db = max_psd;
        res.current_metric = max_psd;

        m_controller.pushSignalMetric(max_psd);
        res.moving_mean = m_controller.calculateMovingMean();
        res.moving_std_dev = m_controller.calculateMovingStdDev();
        res.dynamic_threshold = m_controller.getDynamicThreshold();
        res.anomaly_detected = (max_psd > res.dynamic_threshold);

        char buf[256];
        snprintf(buf, sizeof(buf), "[%s - FREQ_DOMAIN] Peak: %.1fHz @ %.1fdB (Dynamic Threshold: %.1fdB, StdDev: %.2fdB). %s",
                 profile.profile_name.c_str(), res.resonance_freq_hz, res.psd_peak_db, res.dynamic_threshold, res.moving_std_dev,
                 res.anomaly_detected ? "ANOMALY DETECTED" : "Normal");
        res.summary = buf;
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
        if (j.contains("target_frequency")) {
            m_controller.setTargetTuningFrequency(j["target_frequency"].get<float>());
        } else if (j.contains("tuner_string")) {
            std::string s = j["tuner_string"].get<std::string>();
            if (s == "E4") m_controller.setTargetTuningFrequency(329.63f);
            else if (s == "B3") m_controller.setTargetTuningFrequency(246.94f);
            else if (s == "G3") m_controller.setTargetTuningFrequency(196.00f);
            else if (s == "D3") m_controller.setTargetTuningFrequency(146.83f);
            else if (s == "A2") m_controller.setTargetTuningFrequency(110.00f);
            else if (s == "E2") m_controller.setTargetTuningFrequency(82.41f);
            else if (s == "NONE" || s == "OFF") m_controller.setTargetTuningFrequency(0.0f);
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
    return jOut.dump();
}

} // namespace Ronin::Kernel::DSP
