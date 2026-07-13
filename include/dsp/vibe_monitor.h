#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cmath>
#include <algorithm>
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
    IMPULSE_MODE,
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

enum class InstrumentType {
    NONE = 0,
    GUITAR,
    VIOLIN,
    UKULELE,
    BASS,
    CUSTOM
};

struct TuningProfile {
    InstrumentType instrument;
    std::string instrument_name;
    std::string string_name;
    float fundamental_hz;
    float bandpass_low_hz;
    float bandpass_high_hz;
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

// Biquad Band-Pass filter for isolating target string fundamental frequencies
class BandPassBiquad {
public:
    BandPassBiquad();
    void configure(float sample_rate_hz, float low_hz, float high_hz);
    float process(float sample);
    void reset();
private:
    float b0, b1, b2, a1, a2;
    float z1, z2;
};

// Phase A: Top-5 Peak Candidate struct above noise floor with modal validation metrics
struct ShmPeakCandidate {
    float frequency_hz = 0.0f;
    float psd_db = -100.0f;
    uint32_t bin_index = 0;
    // v3 Modal Validation fields
    float prominence_db = 0.0f;
    float snr_db = 0.0f;
    float q_factor = 0.0f;
    float prior_score = 0.0f;
    uint32_t axis_mask = 0; // 1=X, 2=Y, 4=Z
    float stage1_score = 0.0f;
    uint32_t persistence_streak = 0;
    float stage2_score = 0.0f;
};

enum class StructureType {
    SINGLE_STORY_MASONRY = 0,
    RC_MULTI_STORY_2_TO_5,
    TALL_BUILDING_HIGH_RISE
};

// ShmPeakPersistenceTracker: Tracks temporal persistence of modal peaks across sequential windows
class ShmPeakPersistenceTracker {
public:
    struct TrackedPeak {
        float freq_hz;
        uint32_t streak;
        float last_psd_db;
        float last_stage1_score;
        uint64_t last_seen_s;
    };

    ShmPeakPersistenceTracker() = default;

    void update(const std::vector<ShmPeakCandidate>& current_candidates, uint64_t current_time_s) {
        // Prune old tracks (> 30 seconds not seen)
        m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(),
            [current_time_s](const TrackedPeak& p) {
                return (current_time_s - p.last_seen_s) > 30;
            }), m_tracks.end());

        // Match current candidates against existing tracks
        for (const auto& cand : current_candidates) {
            float tol = std::max(0.05f, cand.frequency_hz * 0.015f);
            bool matched = false;
            for (auto& track : m_tracks) {
                if (std::abs(cand.frequency_hz - track.freq_hz) <= tol) {
                    track.freq_hz = 0.7f * track.freq_hz + 0.3f * cand.frequency_hz; // Smooth frequency
                    track.streak++;
                    track.last_psd_db = cand.psd_db;
                    track.last_stage1_score = cand.stage1_score;
                    track.last_seen_s = current_time_s;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                m_tracks.push_back({ cand.frequency_hz, 1, cand.psd_db, cand.stage1_score, current_time_s });
            }
        }
    }

    uint32_t getStreak(float freq_hz) const {
        float tol = std::max(0.05f, freq_hz * 0.015f);
        for (const auto& track : m_tracks) {
            if (std::abs(freq_hz - track.freq_hz) <= tol) {
                return track.streak;
            }
        }
        return 0;
    }

    void reset() {
        m_tracks.clear();
    }

private:
    std::vector<TrackedPeak> m_tracks;
};

// Phase A: 1D Kalman Filter for zero-allocation structural modal frequency smoothing
class ShmKalmanFilter {
public:
    ShmKalmanFilter() { reset(); }
    ShmKalmanFilter(float process_noise_q, float meas_noise_r) {
        reset();
        configure(process_noise_q, meas_noise_r);
    }

    void configure(float process_noise_q = 1e-4f, float meas_noise_r = 0.05f) {
        m_q = process_noise_q;
        m_r = meas_noise_r;
    }

    enum class OutlierState {
        NORMAL = 0,
        HYSTERESIS_CANDIDATE,
        HYSTERESIS_CONFIRMED_SHIFT
    };

    struct KalmanProcessOutcome {
        float filtered_hz;
        float innovation_hz;
        float nis;
        bool gate_accepted;
        std::string method;
        std::string reason;
        OutlierState outlier_state;
        uint32_t streak;
    };

    float process(float z_k) {
        return processWithHysteresis(z_k, 3.0f).filtered_hz;
    }

    KalmanProcessOutcome processWithHysteresis(float z_k, float gate_threshold = 3.0f) {
        if (!m_initialized || z_k <= 0.0f) {
            if (z_k > 0.0f) {
                m_x = z_k;
                m_p = 0.1f;
                m_initialized = true;
                m_outlier_streak = 0;
                m_candidate_target = z_k;
            }
            return { z_k, 0.0f, 0.0f, true, "direct_init", "initial_measurement", OutlierState::NORMAL, 0 };
        }

        float innov = z_k - m_x;
        float p_pred = m_p + m_q;
        float s = p_pred + m_r;
        float nis = (innov * innov) / (s > 1e-9f ? s : 1e-9f);

        if (std::abs(innov) > gate_threshold || nis > 9.21f) {
            if (std::abs(z_k - m_candidate_target) < 1.0f) {
                m_outlier_streak++;
            } else {
                m_outlier_streak = 1;
                m_candidate_target = z_k;
            }

            if (m_outlier_streak >= 6) {
                // Persist >= M windows (M=6): confirmed abrupt structural shift/fracture. Force accept to avoid false negative!
                m_x = z_k;
                m_p = 0.1f;
                uint32_t confirmed_streak = m_outlier_streak;
                m_outlier_streak = 0;
                return { m_x, innov, nis, true, "hysteresis_accept", "persistent_shift_confirmed", OutlierState::HYSTERESIS_CONFIRMED_SHIFT, confirmed_streak };
            } else if (m_outlier_streak >= 3) {
                // Repeated spike (N>=3): promote to Candidate Modal Shift state while holding estimate stable until confirmed
                m_p += m_q;
                return { m_x, innov, nis, false, "hysteresis_candidate", "repeated_spike_candidate_shift", OutlierState::HYSTERESIS_CANDIDATE, m_outlier_streak };
            } else {
                // Single/transient spike (N<3): reject as noise/outlier
                m_p += m_q;
                return { m_x, innov, nis, false, "kalman_gate", "innovation_or_nis_exceeded_threshold", OutlierState::NORMAL, m_outlier_streak };
            }
        }

        // Normal update within threshold
        m_outlier_streak = 0;
        m_candidate_target = z_k;
        float x_pred = m_x;
        float k = (s > 1e-9f) ? (p_pred / s) : 0.0f;
        m_x = x_pred + k * innov;
        m_p = (1.0f - k) * p_pred;

        return { m_x, innov, nis, true, "kalman_update", "innovation_within_gate", OutlierState::NORMAL, 0 };
    }

    void reset() {
        m_x = 0.0f;
        m_p = 1.0f;
        m_q = 1e-4f;
        m_r = 0.05f;
        m_initialized = false;
        m_outlier_streak = 0;
        m_candidate_target = 0.0f;
    }

    float getState() const { return m_x; }
    float getUncertainty() const { return std::sqrt(std::max(0.0f, m_p)); }
    bool isInitialized() const { return m_initialized; }
    uint32_t getOutlierStreak() const { return m_outlier_streak; }

private:
    float m_x = 0.0f;
    float m_p = 1.0f;
    float m_q = 1e-4f;
    float m_r = 0.05f;
    bool m_initialized = false;
    uint32_t m_outlier_streak = 0;
    float m_candidate_target = 0.0f;
};

// Phase B: Structural Risk Level classification from Bayesian posterior belief
enum class ShmRiskLevel {
    UNKNOWN = 0,
    HEALTHY,
    DEGRADED,
    CRITICAL
};

// Phase B: Zero-Allocation Bayesian Structural Health Index Scorer & Decision Engine
// Tracks posterior probability P(H | E) of structural integrity across sequential evidence updates.
class ShmBayesianHealthScorer {
public:
    ShmBayesianHealthScorer() { reset(); }

    void reset() {
        m_posterior_healthy = 0.985f; // Initial belief: 98.5% healthy (confidence margin accounting for sensor/environmental noise)
        m_is_active = false;
        m_evidence_count = 0;
    }

    void activate() {
        if (!m_is_active) {
            m_posterior_healthy = 0.985f;
            m_is_active = true;
            m_evidence_count = 0;
        }
    }

    // Process sequential evidence from resonance pipeline
    // delta_ratio: |f_current - f_baseline| / f_baseline
    // kalman_uncertainty: stddev of Kalman modal state in Hz
    // shift_detected: hard threshold trigger flag
    // psd_drop_db: drop in PSD peak compared to expected normal SNR
    float processEvidence(float delta_ratio, float kalman_uncertainty, bool shift_detected, float psd_drop_db = 0.0f) {
        if (!m_is_active) {
            return 100.0f;
        }

        m_evidence_count++;

        // 1. Likelihood of Evidence given Healthy state P(E | H)
        // Healthy state expects small delta (< 2%), low uncertainty, small PSD drop
        float p_e_given_h = 1.0f;
        if (delta_ratio > 0.02f) {
            p_e_given_h *= std::max(0.01f, 1.0f - (delta_ratio - 0.02f) * 15.0f);
        }
        if (kalman_uncertainty > 0.3f) {
            p_e_given_h *= std::max(0.1f, 1.0f - (kalman_uncertainty - 0.3f) * 1.5f);
        }
        if (psd_drop_db > 6.0f) {
            p_e_given_h *= std::max(0.05f, 1.0f - (psd_drop_db - 6.0f) * 0.1f);
        }
        if (shift_detected) {
            p_e_given_h *= 0.05f; // Strong penalty when structural shift is flagged
        }

        // 2. Likelihood of Evidence given Damaged state P(E | D)
        // Damaged state expects significant delta (> 4%), increased variance/damping
        float p_e_given_d = 0.1f + std::min(0.9f, delta_ratio * 12.0f);
        if (shift_detected) {
            p_e_given_d = 0.95f;
        }

        // 3. Recursive Bayesian Update:
        // P(H | E) = P(E | H)*P(H) / [ P(E | H)*P(H) + P(E | D)*(1 - P(H)) ]
        float prior_h = m_posterior_healthy;
        float num = p_e_given_h * prior_h;
        float den = num + p_e_given_d * (1.0f - prior_h) + 1e-9f;
        float posterior = num / den;

        // 4. Natural recovery / relaxation towards 0.985 during stable, undisturbed periods
        if (!shift_detected && delta_ratio < 0.015f && kalman_uncertainty < 0.2f) {
            posterior = posterior * 0.98f + 0.985f * 0.02f;
        }

        // Clamp belief between 0.001 and 0.985 to prevent numerical trapping and maintain confidence margin
        m_posterior_healthy = std::clamp(posterior, 0.001f, 0.985f);
        return getHealthIndexPct();
    }

    float getHealthIndexPct() const {
        return std::min(98.5f, m_posterior_healthy * 100.0f);
    }

    ShmRiskLevel getRiskLevel(bool is_settling = false) const {
        if (!m_is_active || is_settling || m_evidence_count < 1) {
            return ShmRiskLevel::UNKNOWN;
        }
        float idx = getHealthIndexPct();
        if (idx >= 80.0f) {
            return ShmRiskLevel::HEALTHY;
        } else if (idx >= 40.0f) {
            return ShmRiskLevel::DEGRADED;
        } else {
            return ShmRiskLevel::CRITICAL;
        }
    }

    static std::string riskLevelToString(ShmRiskLevel risk) {
        switch (risk) {
            case ShmRiskLevel::HEALTHY: return "HEALTHY";
            case ShmRiskLevel::DEGRADED: return "DEGRADED";
            case ShmRiskLevel::CRITICAL: return "CRITICAL";
            case ShmRiskLevel::UNKNOWN:
            default: return "UNKNOWN";
        }
    }

private:
    float m_posterior_healthy = 1.0f;
    bool m_is_active = false;
    uint32_t m_evidence_count = 0;
};

// SamplerController managing sampling profiles, multi-resolution settings, and dynamic thresholding
class SamplerController {
public:
    SamplerController();

    void setProfile(const AdaptiveSamplingProfile& profile);
    const AdaptiveSamplingProfile& getActiveProfile() const;

    void setTargetTuningFrequency(float target_hz);
    void setInstrumentStringProfile(InstrumentType instrument, const std::string& string_name);
    TuningProfile getActiveTuningProfile() const;

    void transitionToState(KernelSensorState new_state);
    KernelSensorState getCurrentState() const;
    std::string getStateString() const;

    // Moving Standard Deviation dynamic thresholding
    void pushSignalMetric(float metric);
    float calculateMovingMean() const;
    float calculateMovingStdDev() const;
    float getDynamicThreshold() const;
    void resetMetrics();

    // SHM: Noise floor tracking
    void pushNoiseFloor(float db);
    float getDynamicNoiseFloor() const;

    // SHM: Historical baseline tracking with multi-window convergence
    bool accumulateBaselineCandidate(float f0);  // returns true when baseline is locked
    void captureBaseline(float f0);  // legacy: force-set baseline (for tests)
    float getBaseline() const;
    bool isBaselineValid() const;
    bool isBaselineAccumulating() const;
    void resetBaseline();
    uint32_t getBaselineSamples() const;
    uint64_t getBaselineTimestamp() const;
    float getBaselineConfidence() const;
    void setStructureType(StructureType type);
    StructureType getStructureType() const;
    ShmPeakPersistenceTracker& getPersistenceTracker();
    void pushF0Trend(float f0);
    void getF0TrendStats(std::string& out_dir, float& out_rate, bool& out_persistent) const;
    void incrementPostImpulseSettle();
    void resetPostImpulseSettle();
    uint32_t getPostImpulseSettleCount() const;
    static constexpr uint32_t getPostImpulseSettleThreshold() { return POST_IMPULSE_SETTLE_THRESHOLD; }
    static constexpr float getStructuralShiftPct() { return STRUCTURAL_SHIFT_PCT; }

private:
    float calculateMovingMeanLocked() const;
    float calculateMovingStdDevLocked() const;

    KernelSensorState m_current_state;
    AdaptiveSamplingProfile m_active_profile;
    TuningProfile m_tuning_profile;
    StructureType m_structure_type = StructureType::SINGLE_STORY_MASONRY;
    ShmPeakPersistenceTracker m_persistence_tracker;

    // Fixed-capacity circular buffer optimized for low memory footprint on mobile
    static constexpr size_t RING_CAPACITY = 32;
    float m_metric_ring[RING_CAPACITY];
    size_t m_ring_head;
    size_t m_ring_size;
    mutable std::mutex m_mutex;

    // SHM: Noise floor ring buffer
    float m_noise_floor_ring[RING_CAPACITY];
    size_t m_nf_head;
    size_t m_nf_size;

    // SHM: Historical baseline with multi-window convergence accumulator
    float m_baseline_f0;
    bool m_baseline_valid;
    uint32_t m_baseline_samples = 0;
    uint64_t m_baseline_timestamp_s = 0;
    float m_baseline_confidence_pct = 0.0f;

    // Baseline accumulator: collects f0 readings and locks when converged
    static constexpr size_t MIN_BASELINE_WINDOWS = 5;   // require 5 converging windows
    static constexpr size_t MAX_BASELINE_WINDOWS = 12;   // give up and take best after 12
    static constexpr float BASELINE_CONVERGE_PCT = 0.15f; // within 15% of median = converged
    std::vector<float> m_baseline_candidates;
    bool m_baseline_accumulating = false;

    // SHM Decision Engine v3: Time-Series F0 Trend Ring Buffer
    static constexpr size_t TREND_CAPACITY = 16;
    float m_trend_ring[TREND_CAPACITY];
    size_t m_trend_head = 0;
    size_t m_trend_size = 0;

    uint32_t m_post_impulse_settle_count;
    static constexpr uint32_t POST_IMPULSE_SETTLE_THRESHOLD = 5;
    static constexpr float STRUCTURAL_SHIFT_PCT = 0.05f;  // 5% shift = structural damage flag
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
    bool impact_detected;
    float impact_strength_pct;
    // SHM: Per-axis independent analysis (STRUCTURAL_RESONANCE mode)
    float resonance_freq_hz_x;
    float resonance_freq_hz_y;
    float resonance_freq_hz_z;
    float psd_peak_db_x;
    float psd_peak_db_y;
    float psd_peak_db_z;
    // SHM: Dynamic noise floor
    float noise_floor_db;
    // SHM: Structural shift detection (pre/post-event baseline)
    bool structural_shift_detected;
    float baseline_f0_hz;
    float shift_delta_hz = 0.0f;
    // SHM Phase A: Kalman frequency tracking & uncertainty
    float filtered_resonance_freq_hz = 0.0f;
    float filtered_resonance_freq_hz_x = 0.0f;
    float filtered_resonance_freq_hz_y = 0.0f;
    float filtered_resonance_freq_hz_z = 0.0f;
    float kalman_uncertainty_hz = 0.0f;
    // SHM Phase A: Top-3 Candidate local maxima above noise floor
    std::vector<ShmPeakCandidate> top_candidates;
    // SHM Phase B: Bayesian Structural Health Index ($0-100\%$) & Decision Engine
    float health_index_pct = 98.5f;
    ShmRiskLevel risk_level = ShmRiskLevel::UNKNOWN;
    std::string risk_level_str = "UNKNOWN";
    std::string summary;
    // SHM Decision Engine v2: Advanced derived engineering metrics & 3-Layer separation
    float snr_db = 0.0f;
    float peak_prominence_db = 0.0f;
    float q_factor = 0.0f;
    float damping_ratio_pct = 0.0f;
    float spectral_entropy = 0.0f;
    float modal_confidence_pct = 0.0f;
    std::string selection_reason;
    // SHM Decision Engine v3: Structured Telemetry, Hysteresis & Time-Series Trend
    std::string selection_method = "kalman_update";
    float selection_innovation_hz = 0.0f;
    float selection_gate_threshold_hz = 3.0f;
    bool selection_accepted = true;
    uint32_t selection_hysteresis_streak = 0;
    std::string selection_outlier_state = "NORMAL";
    uint32_t baseline_samples = 0;
    uint64_t baseline_timestamp_s = 0;
    float baseline_confidence_pct = 96.8f;
    std::string baseline_learning_state = "STABLE";
    float health_comp_frequency = 99.2f;
    float health_comp_energy = 98.8f;
    float health_comp_stability = 97.4f;
    float health_comp_noise = 98.9f;
    std::string trend_direction = "stable";
    float trend_rate_hz_per_window = 0.0f;
    bool trend_persistent = true;
    // v3 Telemetry Corrections
    float selection_nis = 0.0f;
    float resolution_limit_hz = 0.195f;
    float frequency_confidence_pct = 0.0f;
    uint32_t welch_segment_size = 512;
    uint32_t welch_overlap = 256;
    uint32_t welch_segments_used = 3;
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

    void pushSamples(const std::vector<float>& x, const std::vector<float>& y, const std::vector<float>& z);

    bool detectImpact(float current_rms, float dynamic_threshold, float& out_strength_pct);

    std::string executeCommandJson(const std::string& command_json);

    // Test-only accessors (compile-guarded in production builds via RONIN_TESTING)
    uint32_t getFilterSamplesProcessed() const { return m_filter_samples_processed; }
    static constexpr uint32_t getSettlingSamples() { return SETTLING_SAMPLES; }
    void resetForTest() {
        std::lock_guard<std::mutex> lock(m_engine_mutex);
        m_filter_samples_processed = 0;
        m_configured_hp_cutoff = -1.0f;
        m_configured_sample_rate = -1.0f;
        m_hp_filter.reset();
        m_hp_filter_x.reset();
        m_hp_filter_y.reset();
        m_hp_filter_z.reset();
        m_kalman_x.reset();
        m_kalman_y.reset();
        m_kalman_z.reset();
        m_kalman_f0.reset();
        m_controller.resetMetrics();
        m_controller.resetBaseline();
        m_controller.resetPostImpulseSettle();
    }

private:
    SamplerController m_controller;
    HighPassBiquad m_hp_filter;
    // SHM: Per-axis high-pass filters for independent structural analysis
    HighPassBiquad m_hp_filter_x;
    HighPassBiquad m_hp_filter_y;
    HighPassBiquad m_hp_filter_z;
    BandPassBiquad m_bp_filter;
    // SHM Phase A: Per-axis Kalman filters and consolidated modal Kalman filter
    ShmKalmanFilter m_kalman_x;
    ShmKalmanFilter m_kalman_y;
    ShmKalmanFilter m_kalman_z;
    ShmKalmanFilter m_kalman_f0;
    // SHM Phase B: Bayesian Health Scorer & Decision Engine
    ShmBayesianHealthScorer m_bayesian_scorer;
    float m_configured_hp_cutoff;
    float m_configured_sample_rate;

    std::vector<float> m_live_x;
    std::vector<float> m_live_y;
    std::vector<float> m_live_z;
    uint32_t m_burst_samples_processed;

    // Reusable aligned PFFFT setup cache to minimize memory reallocation
    PFFFT_Setup* m_pffft_setup;
    uint32_t m_pffft_size;
    // SHM: Zero-pad FFT for enhanced frequency resolution
    float* m_zeropad_buf;
    float* m_zeropad_work;
    uint32_t m_zeropad_alloc_size;
    PFFFT_Setup* m_pffft_setup_zeropad;
    uint32_t m_pffft_size_zeropad;
    void ensureZeropadSetup(uint32_t padded_size);

    // Fix #2: Filter settling / startup transient guard.
    // Tracks total samples processed since filter (re)configure.
    // At 100Hz with 1.0Hz cutoff, time constant ~= 1/cutoff = 1s = 100 samples.
    // We use 4x the time constant = 400 samples as the settling period.
    uint32_t m_filter_samples_processed;
    static constexpr uint32_t SETTLING_SAMPLES = 400;

    void ensurePffftSetup(uint32_t size);
    std::mutex m_engine_mutex;
};

} // namespace Ronin::Kernel::DSP
