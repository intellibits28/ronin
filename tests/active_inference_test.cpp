#include <gtest/gtest.h>
#include "reasoning/active_inference.hpp"
#include <chrono>
#include <random>

using namespace Ronin::Kernel::Reasoning;

TEST(ActiveInferenceTest, NumericalClampingAndStability) {
    // Test extreme low variance -> clamped to MAX_PRECISION (1000.0)
    float prec_high = ActiveInferenceCore::clampPrecision(1e-12f);
    EXPECT_FLOAT_EQ(prec_high, BaselineProfile::MAX_PRECISION);

    // Test extreme high variance -> clamped to MIN_PRECISION (0.001)
    float prec_low = ActiveInferenceCore::clampPrecision(1e9f);
    EXPECT_FLOAT_EQ(prec_low, BaselineProfile::MIN_PRECISION);

    // Test zero variance
    float prec_zero = ActiveInferenceCore::clampPrecision(0.0f);
    EXPECT_LE(prec_zero, BaselineProfile::MAX_PRECISION);
    EXPECT_GE(prec_zero, BaselineProfile::MIN_PRECISION);

    // Test negative variance (sensor anomaly)
    float prec_neg = ActiveInferenceCore::clampPrecision(-5.0f);
    EXPECT_LE(prec_neg, BaselineProfile::MAX_PRECISION);
    EXPECT_GE(prec_neg, BaselineProfile::MIN_PRECISION);
}

TEST(ActiveInferenceTest, DimensionlessMahalanobisNormalization) {
    float obs = 10.5f;
    float pred = 10.0f;
    float sigma_base = 0.25f;

    // Error is (10.5 - 10.0) / 0.25 = 2.0 standard deviations
    float err = ActiveInferenceCore::computeDimensionlessError(obs, pred, sigma_base);
    EXPECT_NEAR(err, 2.0f, 1e-5f);

    // Edge case: baseline sigma extremely small or zero
    float err_safe = ActiveInferenceCore::computeDimensionlessError(obs, pred, 0.0f);
    EXPECT_TRUE(std::isfinite(err_safe));
}

TEST(ActiveInferenceTest, FreeEnergyAndGazeTransitions) {
    BaselineProfile baseline;
    baseline.mean_shm_hz = 12.0f;
    baseline.sigma_shm_hz = 0.20f;
    baseline.sigma_resource = 0.05f;
    baseline.sigma_intent = 0.10f;
    baseline.sigma_noise = 0.05f;

    ActiveInferenceCore core(baseline);

    StateVector state;
    state.shm_freq_hz = 12.0f;
    state.resource_index = 0.95f;
    state.intent_certainty = 1.0f;
    state.env_disturbance = 0.02f;

    // Unit test case 1: Perfect steady state -> QUIESCENT (10 Hz)
    ObservationVector obs_calm;
    obs_calm.shm_freq_hz = 12.01f;
    obs_calm.resource_metric = 0.95f;
    obs_calm.intent_metric = 1.0f;
    obs_calm.noise_metric = 0.02f;

    std::array<float, MODALITY_COUNT> variances = {1.0f, 1.0f, 1.0f, 1.0f};

    FreeEnergyState calm_state = core.computeFreeEnergy(state, obs_calm, variances);
    EXPECT_LT(calm_state.variational_free_energy, ActiveInferenceCore::THRESHOLD_QUIESCENT);
    EXPECT_EQ(calm_state.gaze_state, SensoryGazeState::QUIESCENT);
    EXPECT_EQ(calm_state.target_sample_rate_hz, 10u);

    // Unit test case 2: Moderate shift (0.30 Hz shift = 1.5 sigma) -> VIGILANT (50 Hz)
    ObservationVector obs_mod = obs_calm;
    obs_mod.shm_freq_hz = 12.30f; // 1.5 sigma error on SHM -> F ~ 1.125

    FreeEnergyState mod_state = core.computeFreeEnergy(state, obs_mod, variances);
    EXPECT_GE(mod_state.variational_free_energy, ActiveInferenceCore::THRESHOLD_QUIESCENT);
    EXPECT_LT(mod_state.variational_free_energy, ActiveInferenceCore::THRESHOLD_VIGILANT);
    EXPECT_EQ(mod_state.gaze_state, SensoryGazeState::VIGILANT);
    EXPECT_EQ(mod_state.target_sample_rate_hz, 50u);

    // Unit test case 3: Severe structural shift (2.0 Hz shift) -> ACTIVE_INVESTIGATION (200 Hz)
    ObservationVector obs_severe = obs_calm;
    obs_severe.shm_freq_hz = 14.20f; // 11 sigma shift!

    FreeEnergyState severe_state = core.computeFreeEnergy(state, obs_severe, variances);
    EXPECT_GE(severe_state.variational_free_energy, ActiveInferenceCore::THRESHOLD_VIGILANT);
    EXPECT_EQ(severe_state.gaze_state, SensoryGazeState::ACTIVE_INVESTIGATION);
    EXPECT_EQ(severe_state.target_sample_rate_hz, 200u);
}

TEST(ActiveInferenceTest, EmpiricalBaselineCalibration) {
    BaselineProfile profile;
    profile.resetCalibration();

    EXPECT_FALSE(profile.is_calibrated);

    // Feed 35 synthetic calibration readings centered around 14.5 Hz with +/- 0.1 Hz noise
    std::mt19937 rng(42);
    std::normal_distribution<float> dist(14.5f, 0.12f);

    for (int i = 0; i < 35; ++i) {
        profile.recordCalibrationSample(dist(rng));
    }

    bool success = profile.finalizeCalibration(30);
    EXPECT_TRUE(success);
    EXPECT_TRUE(profile.is_calibrated);
    EXPECT_NEAR(profile.mean_shm_hz, 14.5f, 0.1f);
    EXPECT_GT(profile.sigma_shm_hz, 0.05f);
    EXPECT_LT(profile.sigma_shm_hz, 0.25f);

    // Test insufficient samples fails finalization
    BaselineProfile profile_short;
    profile_short.resetCalibration();
    profile_short.recordCalibrationSample(14.5f);
    EXPECT_FALSE(profile_short.finalizeCalibration(30));
}

TEST(ActiveInferenceTest, HotLoopLatencyBudget) {
    ActiveInferenceCore core;
    StateVector state;
    ObservationVector obs;
    std::array<float, MODALITY_COUNT> variances = {0.04f, 0.01f, 0.02f, 0.01f};

    state.shm_freq_hz = 10.0f;
    obs.shm_freq_hz = 10.2f;

    // Warm up
    for (int i = 0; i < 1000; ++i) {
        volatile auto res = core.computeFreeEnergy(state, obs, variances);
        (void)res;
    }

    // Benchmark 10,000 hot-loop executions
    constexpr int ITERATIONS = 10000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        volatile auto res = core.computeFreeEnergy(state, obs, variances);
        (void)res;
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    double avg_us = (total_ms * 1000.0) / ITERATIONS;

    // Target hypothesis: <= 0.15 ms (150 us). Must easily meet < 15 us on modern hardware!
    EXPECT_LT(avg_us, 150.0);
}
