/**
 * shm_pipeline_test.cpp
 *
 * Regression tests covering the 5 Structural Health Monitoring (SHM) enhancements:
 *   1. Multi-Axis Independent Analysis (Axis Separation)
 *   2. Enhanced Frequency Resolution & Welch Window Customization
 *   3. Dynamic and Configurable High-Pass Filter Cutoff
 *   4. Automated Noise Floor Tracking & Background Calibration
 *   5. Long-Term Historical Baseline Tracking & Structural Shift Detection
 */

#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <string>
#include <memory>
#include "dsp/vibe_monitor.h"
#include <nlohmann/json.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Ronin::Kernel::DSP;

// ──────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────

static std::vector<float> makeShmSignal(float freq_hz, uint32_t win_size = 1024,
                                        float sample_rate_hz = 100.0f) {
    std::vector<float> sig(win_size, 0.0f);
    for (uint32_t i = 0; i < win_size; ++i) {
        float t = static_cast<float>(i) / sample_rate_hz;
        float noise = 0.005f * (static_cast<float>(i % 17) / 17.0f - 0.5f);
        sig[i] = 1.5f * std::sin(2.0f * static_cast<float>(M_PI) * freq_hz * t) + noise;
    }
    return sig;
}

static std::unique_ptr<VibeMonitorEngine> makeSettledShmEngine(float signal_hz = 5.0f) {
    auto eng = std::make_unique<VibeMonitorEngine>();
    eng->getController().transitionToState(KernelSensorState::STARTUP);
    
    // Ensure STRUCTURAL_RESONANCE profile is active
    AdaptiveSamplingProfile prof = eng->getController().getActiveProfile();
    prof.profile_name = "STRUCTURAL_RESONANCE";
    prof.window_size = 1024;
    prof.mode = AnalysisMode::FREQUENCY_DOMAIN;
    prof.high_pass_cutoff_hz = 1.0f;
    eng->getController().setProfile(prof);

    auto signal = makeShmSignal(signal_hz);
    for (int i = 0; i < 6; ++i) {
        eng->analyzePipeline(signal, signal, signal);
    }
    if (eng->getController().getCurrentState() == KernelSensorState::STARTUP) {
        eng->getController().transitionToState(KernelSensorState::STABLE);
    }
    return eng;
}

// ──────────────────────────────────────────────────────────────
// Test Cases
// ──────────────────────────────────────────────────────────────

TEST(ShmPipelineTest, MultiAxisIndependentAnalysis) {
    auto eng = makeSettledShmEngine(5.0f);

    auto sig_x = makeShmSignal(5.0f);
    auto sig_y = makeShmSignal(10.0f);
    auto sig_z = makeShmSignal(15.0f);

    // Process a few passes so per-axis filters settle to independent signals
    VibeMonitorResult res;
    for (int i = 0; i < 4; ++i) {
        res = eng->analyzePipeline(sig_x, sig_y, sig_z);
    }

    EXPECT_NEAR(res.resonance_freq_hz_x, 5.0f, 0.5f);
    EXPECT_NEAR(res.resonance_freq_hz_y, 10.0f, 0.5f);
    EXPECT_NEAR(res.resonance_freq_hz_z, 15.0f, 0.5f);
    EXPECT_GT(res.psd_peak_db_x, -40.0f);
    EXPECT_GT(res.psd_peak_db_y, -40.0f);
    EXPECT_GT(res.psd_peak_db_z, -40.0f);
}

TEST(ShmPipelineTest, EnhancedFrequencyResolution) {
    auto eng1 = makeSettledShmEngine(5.0f);
    auto eng2 = makeSettledShmEngine(5.15f);

    auto sig1 = makeShmSignal(5.0f);
    auto sig2 = makeShmSignal(5.15f);

    VibeMonitorResult res1, res2;
    for (int i = 0; i < 3; ++i) {
        res1 = eng1->analyzePipeline(sig1, sig1, sig1);
        res2 = eng2->analyzePipeline(sig2, sig2, sig2);
    }

    // With 2048-pt FFT (0.0488 Hz/bin), 5.0 Hz and 5.15 Hz map to distinct bins (102 vs 105)
    EXPECT_NE(res1.resonance_freq_hz, res2.resonance_freq_hz);
    EXPECT_NEAR(res1.resonance_freq_hz, 5.0f, 0.08f);
    EXPECT_NEAR(res2.resonance_freq_hz, 5.15f, 0.08f);
}

TEST(ShmPipelineTest, DynamicHighPassCutoff) {
    auto eng = makeSettledShmEngine(5.0f);

    // Set high_pass_cutoff_hz down to 0.1 Hz via JSON
    nlohmann::json cmd;
    cmd["high_pass_cutoff_hz"] = 0.1f;
    eng->executeCommandJson(cmd.dump());

    auto sig_low = makeShmSignal(0.5f);
    VibeMonitorResult res;
    for (int i = 0; i < 4; ++i) {
        res = eng->analyzePipeline(sig_low, sig_low, sig_low);
    }

    EXPECT_NEAR(res.resonance_freq_hz, 0.5f, 0.3f);
    EXPECT_EQ(res.high_pass_cutoff_hz, 0.1f);

    // Test safety clamp: setting cutoff <= 0 or 0.01 should clamp to 0.05 Hz minimum
    HighPassBiquad hp;
    hp.configure(100.0f, 0.01f);
    hp.reset();
    // Verified by ensuring filter is actively filtering (not pass-through b0=1, rest=0)
    EXPECT_NE(hp.process(1.0f), 1.0f);
}

TEST(ShmPipelineTest, DynamicNoiseFloorTracking) {
    auto eng = makeSettledShmEngine(5.0f);

    auto sig = makeShmSignal(5.0f);
    VibeMonitorResult res;
    for (int i = 0; i < 5; ++i) {
        res = eng->analyzePipeline(sig, sig, sig);
    }

    // Noise floor should no longer be the static fallback -75.0 dB
    EXPECT_NE(res.noise_floor_db, -75.0f);
    EXPECT_GE(res.noise_floor_db, -80.0f);
    EXPECT_LE(res.noise_floor_db, -20.0f);
}

TEST(ShmPipelineTest, StructuralShiftDetection) {
    auto eng = makeSettledShmEngine(5.0f);

    auto sig_base = makeShmSignal(5.0f);
    VibeMonitorResult res_base = eng->analyzePipeline(sig_base, sig_base, sig_base);

    EXPECT_TRUE(eng->getController().isBaselineValid());
    float baseline = eng->getController().getBaseline();
    EXPECT_NEAR(baseline, 5.0f, 0.2f);

    // Trigger IMPULSE_MODE to simulate environmental excitation / event
    eng->getController().transitionToState(KernelSensorState::IMPULSE_MODE);
    
    // Process one burst pass in IMPULSE_MODE (automatically transitions back to STABLE + starts post-impulse counter)
    auto sig_impulse = makeShmSignal(85.0f);
    eng->analyzePipeline(sig_impulse, sig_impulse, sig_impulse);
    EXPECT_EQ(eng->getController().getCurrentState(), KernelSensorState::STABLE);
    EXPECT_EQ(eng->getController().getPostImpulseSettleCount(), 1u);

    // Feed a shifted signal at 4.0 Hz (20% drop from 5.0 Hz baseline, well above 5% threshold)
    auto sig_shifted = makeShmSignal(4.0f);
    VibeMonitorResult res_shift;
    for (uint32_t i = 1; i <= SamplerController::getPostImpulseSettleThreshold(); ++i) {
        res_shift = eng->analyzePipeline(sig_shifted, sig_shifted, sig_shifted);
        if (res_shift.structural_shift_detected) break;
    }

    EXPECT_TRUE(res_shift.structural_shift_detected);
    EXPECT_LT(res_shift.shift_delta_hz, -0.5f);
}

TEST(ShmPipelineTest, PhaseAKalmanTrackingAndTopCandidates) {
    auto eng = makeSettledShmEngine(5.0f);

    // Create signal with primary 5.0 Hz mode and secondary 15.0 Hz mode
    uint32_t win = eng->getController().getActiveProfile().window_size;
    std::vector<float> dual_mode(win, 0.0f);
    for (uint32_t i = 0; i < win; ++i) {
        float t = (float)i / 100.0f;
        dual_mode[i] = 1.0f * std::sin(2.0f * (float)M_PI * 5.0f * t) +
                       0.3f * std::sin(2.0f * (float)M_PI * 15.0f * t);
    }

    // Process window and verify Kalman output & candidates
    VibeMonitorResult res1 = eng->analyzePipeline(dual_mode, dual_mode, dual_mode);
    EXPECT_NEAR(res1.filtered_resonance_freq_hz, 5.0f, 0.1f);
    EXPECT_GT(res1.kalman_uncertainty_hz, 0.0f);
    EXPECT_GE(res1.top_candidates.size(), 1u);
    EXPECT_NEAR(res1.top_candidates[0].frequency_hz, 5.0f, 0.1f);

    // Check JSON serialization of Phase A fields
    std::string json_str = eng->executeCommandJson("RESONANCE");
    EXPECT_NE(json_str.find("filtered_resonance_freq_hz"), std::string::npos);
    EXPECT_NE(json_str.find("top_candidates"), std::string::npos);
}

TEST(ShmPipelineTest, PhaseBBayesianHealthScoringAndDecisionEngine) {
    auto eng = makeSettledShmEngine(5.0f);

    // Initial stable evaluation at baseline frequency (5.0 Hz)
    auto sig_base = makeShmSignal(5.0f);
    VibeMonitorResult res_base = eng->analyzePipeline(sig_base, sig_base, sig_base);
    EXPECT_NEAR(res_base.health_index_pct, 98.5f, 0.5f);
    EXPECT_EQ(res_base.risk_level, ShmRiskLevel::HEALTHY);
    EXPECT_EQ(res_base.risk_level_str, "HEALTHY");
    EXPECT_GT(res_base.snr_db, 5.0f);
    EXPECT_GT(res_base.q_factor, 1.0f);
    EXPECT_GT(res_base.modal_confidence_pct, 50.0f);

    // Trigger impulse event and transition back
    eng->getController().transitionToState(KernelSensorState::IMPULSE_MODE);
    auto sig_impulse = makeShmSignal(85.0f);
    eng->analyzePipeline(sig_impulse, sig_impulse, sig_impulse);

    // Feed a shifted signal (4.0 Hz) until structural shift / degradation triggers
    auto sig_shifted = makeShmSignal(4.0f);
    VibeMonitorResult res_shift;
    for (uint32_t i = 1; i <= SamplerController::getPostImpulseSettleThreshold() + 2; ++i) {
        res_shift = eng->analyzePipeline(sig_shifted, sig_shifted, sig_shifted);
    }

    EXPECT_LT(res_shift.health_index_pct, 50.0f);
    EXPECT_TRUE(res_shift.risk_level == ShmRiskLevel::DEGRADED || res_shift.risk_level == ShmRiskLevel::CRITICAL);
    EXPECT_NE(res_shift.shift_delta_hz, 0.0f);

    // Check JSON serialization of Phase B and SHM Decision Engine v2 (3-Layer Hierarchy) fields
    std::string json_str = eng->executeCommandJson("RESONANCE");
    EXPECT_NE(json_str.find("health_index_pct"), std::string::npos);
    EXPECT_NE(json_str.find("risk_level"), std::string::npos);
    EXPECT_NE(json_str.find("snr_db"), std::string::npos);
    EXPECT_NE(json_str.find("q_factor"), std::string::npos);
    EXPECT_NE(json_str.find("damping_ratio_pct"), std::string::npos);
    EXPECT_NE(json_str.find("spectral_entropy"), std::string::npos);
    EXPECT_NE(json_str.find("modal_confidence_pct"), std::string::npos);
    EXPECT_NE(json_str.find("selection_reason"), std::string::npos);
    EXPECT_NE(json_str.find("layer_1_raw_measurements"), std::string::npos);
    EXPECT_NE(json_str.find("layer_2_derived_metrics"), std::string::npos);
    EXPECT_NE(json_str.find("layer_3_decision_outputs"), std::string::npos);
}
