#include <gtest/gtest.h>
#include "reasoning/active_inference.hpp"
#include "reasoning/policy_router.hpp"
#include "long_term_memory.h"
#include "capability_policy_engine.h"
#include "dsp/resonance_analyzer.h"
#include <chrono>
#include <cstdio>
#include <random>

using namespace Ronin::Kernel::Reasoning;
using Ronin::Kernel::Memory::LongTermMemory;
using Ronin::Kernel::Memory::SourceType;
using Ronin::Kernel::CapabilityPolicyEngine;
using Ronin::Kernel::DSP::ResonanceAnalyzer;

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

TEST(ActiveInferenceTest, Phase1_DualGateRagWakeup_EdgeCasesAndSharpPeak) {
    // 1. Empty BM25 scores -> high epistemic uncertainty -> INSPECT_DOC (wakeup embedding)
    auto empty_dec = ExpectedFreeEnergyRouter::evaluateRagWakeup({});
    EXPECT_TRUE(empty_dec.should_wakeup_embedding);
    EXPECT_EQ(empty_dec.policy, PolicyPrimitive::INSPECT_DOC);

    // 2. Uniform low BM25 scores -> fails both Gate 1 (|s_1| > 0.5) and Gate 2 (H_hat < 0.35)
    auto uniform_low = ExpectedFreeEnergyRouter::evaluateRagWakeup({0.45f, 0.42f, 0.40f, 0.39f, 0.38f});
    EXPECT_TRUE(uniform_low.should_wakeup_embedding);
    EXPECT_EQ(uniform_low.policy, PolicyPrimitive::INSPECT_DOC);
    EXPECT_GT(uniform_low.normalized_entropy, 0.80f);

    // 3. Sharp single BM25 peak -> satisfies Gate 1 (|s_1| > 0.5) and Gate 2 (H_hat < 0.35) -> LEXICAL_ONLY
    auto sharp_peak = ExpectedFreeEnergyRouter::evaluateRagWakeup({4.5f, 0.3f, 0.2f, 0.1f, 0.05f});
    EXPECT_FALSE(sharp_peak.should_wakeup_embedding);
    EXPECT_EQ(sharp_peak.policy, PolicyPrimitive::LEXICAL_ONLY);
    EXPECT_LT(sharp_peak.normalized_entropy, 0.35f);

    // 4. Single strong hit above theta_min -> LEXICAL_ONLY
    auto single_strong = ExpectedFreeEnergyRouter::evaluateRagWakeup({2.1f});
    EXPECT_FALSE(single_strong.should_wakeup_embedding);
    EXPECT_EQ(single_strong.policy, PolicyPrimitive::LEXICAL_ONLY);
}

TEST(ActiveInferenceTest, Phase2_BrainRouter_PrivacyCostInjectionAndProbeEscalation) {
    // 1. PII Risk detected -> +100.0 pragmatic penalty strictly blocks cloud escalation even at max uncertainty
    auto pii_blocked = ExpectedFreeEnergyRouter::evaluateBrainRoute(
        "Analyze this complex distributed architecture and prove theorem",
        /*resource_index=*/0.8f,
        /*pii_risk_flag=*/1.0f,
        /*early_probe_entropy=*/0.98f);
    EXPECT_FALSE(pii_blocked.escalate_to_cloud);
    EXPECT_TRUE(pii_blocked.privacy_blocked);
    EXPECT_EQ(pii_blocked.policy, PolicyPrimitive::EXEC_TOOL);
    EXPECT_GT(pii_blocked.cloud_efe, 100.0f);

    // 2. Clean complex prompt + high 8-token probe uncertainty -> ESCALATE_CLOUD
    auto escalate = ExpectedFreeEnergyRouter::evaluateBrainRoute(
        "Design a multi-cluster Raft consensus algorithm and analyze mathematical proof of liveness",
        /*resource_index=*/0.8f,
        /*pii_risk_flag=*/0.0f,
        /*early_probe_entropy=*/0.92f);
    EXPECT_TRUE(escalate.escalate_to_cloud);
    EXPECT_EQ(escalate.policy, PolicyPrimitive::ESCALATE_CLOUD);

    // 3. Simple local prompt -> EXEC_TOOL (local Gemma 4 E2B)
    auto local_ok = ExpectedFreeEnergyRouter::evaluateBrainRoute(
        "Hello Ronin",
        /*resource_index=*/1.0f,
        /*pii_risk_flag=*/0.0f,
        /*early_probe_entropy=*/0.05f);
    EXPECT_FALSE(local_ok.escalate_to_cloud);
    EXPECT_EQ(local_ok.policy, PolicyPrimitive::EXEC_TOOL);
}

TEST(ActiveInferenceTest, Phase2_MultiHopResearch_QuadraticCostAndEarlyStop) {
    // 1. Hop 0 with poor coverage (0.10) -> should trigger RESEARCH_HOP
    auto hop1 = ExpectedFreeEnergyRouter::evaluateResearchHop(
        /*current_hop=*/0,
        /*coverage_ratio=*/0.10f,
        /*prev_free_energy=*/1.5f);
    EXPECT_TRUE(hop1.should_continue_foraging);
    EXPECT_EQ(hop1.policy, PolicyPrimitive::RESEARCH_HOP);

    // 2. Diminishing returns early-stop (Delta F < min_delta_f = 0.12)
    auto early_stop = ExpectedFreeEnergyRouter::evaluateResearchHop(
        /*current_hop=*/1,
        /*coverage_ratio=*/0.20f,
        /*prev_free_energy=*/0.82f); // residual is 1.0 - 0.2 = 0.80 -> Delta F = 0.02 < 0.12
    EXPECT_FALSE(early_stop.should_continue_foraging);
    EXPECT_EQ(early_stop.policy, PolicyPrimitive::IDLE);

    // 3. Hard cap at max_hops = 2
    auto max_hop_cap = ExpectedFreeEnergyRouter::evaluateResearchHop(
        /*current_hop=*/2,
        /*coverage_ratio=*/0.05f,
        /*prev_free_energy=*/1.5f);
    EXPECT_FALSE(max_hop_cap.should_continue_foraging);
}

TEST(ActiveInferenceTest, Phase3_McpSlidingWindowAnomalyGate_And_SurpriseDrivenLtm) {
    CapabilityPolicyEngine engine;
    engine.resetMcpMonitor();

    // 1. Benign read_file call -> ALLOW
    auto benign = engine.evaluateMcpToolCall("read_file", "/workspace/README.md", "summarize readme", 1000);
    EXPECT_EQ(benign.decision, Ronin::Kernel::PolicyDecision::ALLOW);
    EXPECT_FALSE(benign.requires_hitl_confirm);
    EXPECT_LT(benign.contextual_surprise, 0.70f);

    // 2. Sensitive path traversal -> escalates ALLOW tool to DEFER (MCP_HITL_CONFIRM)
    auto sensitive = engine.evaluateMcpToolCall("read_file", "../../.ssh/id_rsa", "weather today", 4000);
    EXPECT_EQ(sensitive.decision, Ronin::Kernel::PolicyDecision::DEFER);
    EXPECT_TRUE(sensitive.requires_hitl_confirm);
    EXPECT_GT(sensitive.contextual_surprise, 0.70f);

    // 3. Rapid burst anomaly (6 calls within 150ms) -> escalates ALLOW tool to DEFER
    engine.resetMcpMonitor();
    uint64_t t0 = 10000;
    for (int i = 0; i < 5; ++i) {
        engine.evaluateMcpToolCall("list_files", "/workspace/src", "list files", t0 + i * 20);
    }
    auto burst = engine.evaluateMcpToolCall("list_files", "/workspace/src", "list files", t0 + 120);
    EXPECT_EQ(burst.decision, Ronin::Kernel::PolicyDecision::DEFER);
    EXPECT_TRUE(burst.requires_hitl_confirm);
    EXPECT_GT(burst.contextual_surprise, 0.70f);

    // 4. Surprise-Driven LTM Fact Tagging & Nightly Reflection Reconciliation
    const char* db_path = "test_active_inf_surprise.db";
    std::remove(db_path);
    {
        LongTermMemory ltm(db_path);
        // Initial fact: moderate novelty surprise (~0.225)
        EXPECT_TRUE(ltm.storeFact("User", "preferred_lang", "English", SourceType::USER_EXPLICIT, 0.9f));
        // Reinforcing same fact: very low surprise (0.05)
        EXPECT_TRUE(ltm.storeFact("User", "preferred_lang", "English", SourceType::USER_EXPLICIT, 0.9f));
        // Contradicting prior belief: high epistemic surprise (> 0.75)
        EXPECT_TRUE(ltm.storeFact("User", "preferred_lang", "Myanmar", SourceType::USER_EXPLICIT, 0.95f));

        auto top_surprises = ltm.getTopSurprisingFacts(5, 0.50f);
        ASSERT_FALSE(top_surprises.empty());
        EXPECT_EQ(top_surprises[0].entity, "User");
        EXPECT_EQ(top_surprises[0].value, "Myanmar");
        EXPECT_GT(top_surprises[0].surprise_score, 0.75f);

        // Reconcile surprise during reflection
        EXPECT_TRUE(ltm.resolveFactSurprise("User", "preferred_lang", 0.10f));
        auto after_resolve = ltm.getTopSurprisingFacts(5, 0.50f);
        EXPECT_TRUE(after_resolve.empty());
    }
    std::remove(db_path);
}

TEST(ActiveInferenceTest, Phase4_LockFreeAudioKalmanPrecision) {
    // 1. Low ambient noise -> high sensory precision -> high Kalman gain (tracks observation)
    auto clean_update = ResonanceAnalyzer::computeAudioKalmanPrecision(
        /*freq_obs_hz=*/18.0f,
        /*prior_freq_hz=*/12.0f,
        /*prior_precision=*/0.5f,
        /*noise_rms=*/0.02f,
        /*beta=*/5.0f);
    EXPECT_GT(clean_update.sensory_precision, 0.85f);
    EXPECT_GT(clean_update.kalman_gain, 0.60f);
    EXPECT_GT(clean_update.posterior_freq_hz, 15.5f);

    // 2. High ambient noise -> low sensory precision -> low Kalman gain (clings to prior belief)
    auto noisy_update = ResonanceAnalyzer::computeAudioKalmanPrecision(
        /*freq_obs_hz=*/45.0f,
        /*prior_freq_hz=*/12.0f,
        /*prior_precision=*/1.0f,
        /*noise_rms=*/1.20f,
        /*beta=*/5.0f);
    EXPECT_LT(noisy_update.sensory_precision, 0.01f);
    EXPECT_LT(noisy_update.kalman_gain, 0.01f);
    EXPECT_NEAR(noisy_update.posterior_freq_hz, 12.0f, 0.5f);
}
