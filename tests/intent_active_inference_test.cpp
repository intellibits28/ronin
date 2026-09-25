#include <gtest/gtest.h>
#include "reasoning/policy_router.hpp"
#include "intent_engine.h"
#include <chrono>
#include <cmath>
#include <iostream>

using namespace Ronin::Kernel::Reasoning;
using namespace Ronin::Kernel::Intent;

// ============================================================================
// 1. Slot Entropy Formulation Tests
// ============================================================================
TEST(IntentActiveInferenceTest, SlotEntropyFormulation) {
    SlotEntropyContext ctx_full{1.0f, 2, 0};
    EXPECT_NEAR(ctx_full.computeEntropy(), 0.0f, 1e-4f);

    // "ဖိုင်ပို့ပေးပါ" scenario: conf=0.85, 1 missing out of 1
    SlotEntropyContext ctx_missing{0.85f, 1, 1};
    // H = 0.4 * (1.0 - 0.85) + 0.6 * (1 / 1) = 0.06 + 0.60 = 0.66
    EXPECT_NEAR(ctx_missing.computeEntropy(), 0.66f, 1e-4f);
    EXPECT_GT(ctx_missing.computeEntropy(), ExpectedFreeEnergyRouter::THRESHOLD_EPISTEMIC);

    // Completely satisfied query
    SlotEntropyContext ctx_satisfied{0.95f, 1, 0};
    // H = 0.4 * (1.0 - 0.95) + 0.6 * 0 = 0.02
    EXPECT_NEAR(ctx_satisfied.computeEntropy(), 0.02f, 1e-4f);
    EXPECT_LE(ctx_satisfied.computeEntropy(), ExpectedFreeEnergyRouter::THRESHOLD_EPISTEMIC);
}

// ============================================================================
// 2. Query Analysis & Epistemic Ambiguity Detection
// ============================================================================
TEST(IntentActiveInferenceTest, QueryAnalysisMyanmarAndEnglish) {
    // File Share with missing recipient and file target -> CLARIFY (2 of 2 missing)
    auto ctx1 = ExpectedFreeEnergyRouter::analyzeQuery("ဖိုင်ပို့ပေးပါ", "SEND_SMS", 0.85f);
    EXPECT_GT(ctx1.computeEntropy(), ExpectedFreeEnergyRouter::THRESHOLD_EPISTEMIC);
    EXPECT_EQ(ctx1.total_required_slots, 2u);
    EXPECT_EQ(ctx1.missing_required_slots, 2u);

    // SMS with recipient present -> EXECUTE
    auto ctx2 = ExpectedFreeEnergyRouter::analyzeQuery("0912345678 ဆီ စာပို့ပါ", "SEND_SMS", 0.95f);
    EXPECT_LE(ctx2.computeEntropy(), ExpectedFreeEnergyRouter::THRESHOLD_EPISTEMIC);
    EXPECT_EQ(ctx2.missing_required_slots, 0u);

    // Alarm without time -> CLARIFY
    auto ctx3 = ExpectedFreeEnergyRouter::analyzeQuery("နှိုးစက် ပေးပါ", "SET_ALARM", 0.85f);
    EXPECT_GT(ctx3.computeEntropy(), ExpectedFreeEnergyRouter::THRESHOLD_EPISTEMIC);

    // Alarm with explicit time -> EXECUTE
    auto ctx4 = ExpectedFreeEnergyRouter::analyzeQuery("မနက်ဖြန် မနက် ၇ နာရီ နှိုးပါ", "SET_ALARM", 0.95f);
    EXPECT_LE(ctx4.computeEntropy(), ExpectedFreeEnergyRouter::THRESHOLD_EPISTEMIC);

    // Self-contained command -> EXECUTE
    auto ctx5 = ExpectedFreeEnergyRouter::analyzeQuery("turn on flashlight", "FLASHLIGHT", 0.98f);
    EXPECT_LE(ctx5.computeEntropy(), ExpectedFreeEnergyRouter::THRESHOLD_EPISTEMIC);
}

// ============================================================================
// 3. Expected Free Energy Policy Selection Under Missing Slots
// ============================================================================
TEST(IntentActiveInferenceTest, EpistemicClarificationWhenSlotsMissing) {
    ExpectedFreeEnergyRouter router;

    StateVector state;
    state.shm_freq_hz = 10.0f;
    state.resource_index = 1.0f;
    state.intent_certainty = 0.34f; // 1.0 - 0.66
    state.env_disturbance = 0.0f;

    StateVector priors;
    priors.shm_freq_hz = 10.0f;
    priors.resource_index = 1.0f;
    priors.intent_certainty = 1.0f;
    priors.env_disturbance = 0.0f;

    std::array<float, MODALITY_COUNT> precisions = {16.0f, 400.0f, 100.0f, 400.0f};
    std::array<float, MODALITY_COUNT> variances = {0.0625f, 0.0025f, 0.4356f, 0.0025f};

    SlotEntropyContext slot_ctx{0.85f, 1, 1}; // H = 0.66 > 0.45

    auto decision = router.evaluate(state, priors, precisions, variances, slot_ctx);

    EXPECT_TRUE(decision.requires_clarification);
    EXPECT_EQ(decision.optimal_policy, PolicyPrimitive::CLARIFY_USER);

    // Epistemic value of CLARIFY_USER should be positive (resolving intent uncertainty)
    const auto& clarify_eval = decision.candidate_evaluations[static_cast<size_t>(PolicyPrimitive::CLARIFY_USER)];
    const auto& exec_eval = decision.candidate_evaluations[static_cast<size_t>(PolicyPrimitive::EXEC_TOOL)];

    EXPECT_GT(clarify_eval.epistemic_value, 0.0f);
    EXPECT_LT(clarify_eval.expected_free_energy, exec_eval.expected_free_energy);
    EXPECT_GT(clarify_eval.selection_probability, exec_eval.selection_probability);
}

// ============================================================================
// 4. Pragmatic Tool Execution When Slots Are Complete
// ============================================================================
TEST(IntentActiveInferenceTest, PragmaticExecutionWhenSlotsComplete) {
    ExpectedFreeEnergyRouter router;

    StateVector state;
    state.shm_freq_hz = 10.0f;
    state.resource_index = 1.0f;
    state.intent_certainty = 0.98f;
    state.env_disturbance = 0.0f;

    StateVector priors;
    priors.shm_freq_hz = 10.0f;
    priors.resource_index = 1.0f;
    priors.intent_certainty = 1.0f;
    priors.env_disturbance = 0.0f;

    std::array<float, MODALITY_COUNT> precisions = {16.0f, 400.0f, 100.0f, 400.0f};
    std::array<float, MODALITY_COUNT> variances = {0.0625f, 0.0025f, 0.01f, 0.0025f};

    SlotEntropyContext slot_ctx{0.95f, 1, 0}; // H = 0.02 <= 0.45

    auto decision = router.evaluate(state, priors, precisions, variances, slot_ctx);

    EXPECT_FALSE(decision.requires_clarification);
    EXPECT_EQ(decision.optimal_policy, PolicyPrimitive::EXEC_TOOL);

    const auto& clarify_eval = decision.candidate_evaluations[static_cast<size_t>(PolicyPrimitive::CLARIFY_USER)];
    const auto& exec_eval = decision.candidate_evaluations[static_cast<size_t>(PolicyPrimitive::EXEC_TOOL)];

    // Exec tool should have lower EFE than CLARIFY (due to conversational friction penalty on clarify)
    EXPECT_LT(exec_eval.expected_free_energy, clarify_eval.expected_free_energy);
    EXPECT_GT(exec_eval.selection_probability, clarify_eval.selection_probability);
}

// ============================================================================
// 5. Cross-Modal Routing: Anomaly & Self-Healing
// ============================================================================
TEST(IntentActiveInferenceTest, CrossModalAnomalyAndSelfHealing) {
    ExpectedFreeEnergyRouter router;

    StateVector priors{10.0f, 1.0f, 1.0f, 0.0f};
    std::array<float, MODALITY_COUNT> precisions = {16.0f, 400.0f, 100.0f, 400.0f};
    SlotEntropyContext calm_slot{1.0f, 0, 0};

    // Case A: Severe structural vibration anomaly (SHM variance high)
    {
        StateVector shm_anomaly_state{11.8f, 1.0f, 1.0f, 0.2f};
        std::array<float, MODALITY_COUNT> shm_variances = {0.85f, 0.0025f, 0.01f, 0.0025f};

        auto dec = router.evaluate(shm_anomaly_state, priors, precisions, shm_variances, calm_slot);
        EXPECT_EQ(dec.optimal_policy, PolicyPrimitive::SAMPLE_HIRES);
    }

    // Case B: Severe Thermal/Battery degradation (resource = 0.35)
    {
        StateVector resource_degraded_state{10.0f, 0.35f, 1.0f, 0.0f};
        std::array<float, MODALITY_COUNT> res_variances = {0.0625f, 0.09f, 0.01f, 0.0025f};

        auto dec = router.evaluate(resource_degraded_state, priors, precisions, res_variances, calm_slot);
        EXPECT_EQ(dec.optimal_policy, PolicyPrimitive::SELF_HEAL);
    }

    // Case C: Quiescent homeostatic state (everything normal, no pending query)
    {
        SlotEntropyContext quiescent_slot{0.0f, 0, 0};
        StateVector quiescent_state{10.0f, 1.0f, 1.0f, 0.0f};
        std::array<float, MODALITY_COUNT> quiet_variances = {0.0625f, 0.0025f, 0.01f, 0.0025f};

        auto dec = router.evaluate(quiescent_state, priors, precisions, quiet_variances, quiescent_slot);
        EXPECT_EQ(dec.optimal_policy, PolicyPrimitive::IDLE);
    }
}

// ============================================================================
// 6. Softmax Probability Integrity
// ============================================================================
TEST(IntentActiveInferenceTest, SoftmaxProbabilityIntegrity) {
    ExpectedFreeEnergyRouter router;

    StateVector state{10.2f, 0.85f, 0.60f, 0.1f};
    StateVector priors{10.0f, 1.0f, 1.0f, 0.0f};
    std::array<float, MODALITY_COUNT> precisions = {16.0f, 100.0f, 50.0f, 100.0f};
    std::array<float, MODALITY_COUNT> variances = {0.1f, 0.01f, 0.16f, 0.01f};
    SlotEntropyContext slot_ctx{0.60f, 2, 1};

    auto decision = router.evaluate(state, priors, precisions, variances, slot_ctx);

    float sum_p = 0.0f;
    float max_p = 0.0f;
    PolicyPrimitive max_p_policy = PolicyPrimitive::IDLE;

    for (const auto& eval : decision.candidate_evaluations) {
        EXPECT_GT(eval.selection_probability, 0.0f);
        EXPECT_LE(eval.selection_probability, 1.0f);
        sum_p += eval.selection_probability;

        if (eval.selection_probability > max_p) {
            max_p = eval.selection_probability;
            max_p_policy = eval.policy;
        }
    }

    EXPECT_NEAR(sum_p, 1.0f, 1e-4f);
    EXPECT_EQ(decision.optimal_policy, max_p_policy);
}

// ============================================================================
// 7. Latency Budget & IntentEngine Integration Benchmark
// ============================================================================
TEST(IntentActiveInferenceTest, LatencyBudgetAndIntentEngineIntegration) {
    IntentEngine engine;

    // Test IntentEngine integration
    auto dec1 = engine.evaluatePolicy("ဖိုင်ပို့ပေးပါ");
    EXPECT_TRUE(dec1.requires_clarification);
    EXPECT_EQ(dec1.optimal_policy, PolicyPrimitive::CLARIFY_USER);

    auto dec2 = engine.evaluatePolicy("0912345678 ဆီ စာပို့ပါ");
    EXPECT_FALSE(dec2.requires_clarification);
    EXPECT_EQ(dec2.optimal_policy, PolicyPrimitive::EXEC_TOOL);

    // Benchmark 1,000 iterations
    constexpr size_t ITERATIONS = 1000;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < ITERATIONS; ++i) {
        auto d = engine.evaluatePolicy("0912345678 ဆီ စာပို့ပါ");
        (void)d;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    double avg_us = (total_ms * 1000.0) / ITERATIONS;

    std::cout << "\n======================================================\n";
    std::cout << "  LEVEL 2 ACTIVE INFERENCE POLICY ROUTER BENCHMARK\n";
    std::cout << "  Iterations:     " << ITERATIONS << "\n";
    std::cout << "  Total Latency:  " << total_ms << " ms\n";
    std::cout << "  Avg Latency:    " << avg_us << " microseconds/evaluation\n";
    std::cout << "======================================================\n";

    // Target is < 3.5 ms (3500 us), budget limit is < 10.0 ms
    EXPECT_LT(avg_us, 3500.0);
}
