#pragma once

#include "reasoning/active_inference.hpp"
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <cmath>

namespace Ronin::Kernel::Reasoning {

/**
 * Phase 1: Dual-Gate BM25 Entropy Evaluation for Smart RAG Wakeup (/docsearch)
 */
struct RagWakeupDecision {
    PolicyPrimitive policy{PolicyPrimitive::INSPECT_DOC};
    float top_bm25_magnitude{0.0f};
    float normalized_entropy{1.0f};
    float expected_free_energy_lexical{0.0f};
    float expected_free_energy_semantic{0.0f};
    bool should_wakeup_embedding{true};
};

/**
 * Phase 2: Privacy-Weighted Brain Routing & 8-Token Speculative Early-Exit
 */
struct BrainRouteDecision {
    PolicyPrimitive policy{PolicyPrimitive::EXEC_TOOL};
    float prior_epistemic_uncertainty{0.0f};
    float probe_entropy{0.0f};
    float local_efe{0.0f};
    float cloud_efe{0.0f};
    bool escalate_to_cloud{false};
    bool privacy_blocked{false};
};

/**
 * Phase 2: Multi-Hop Epistemic Foraging Loop (/research) with Quadratic Hop Cost
 */
struct ResearchHopDecision {
    PolicyPrimitive policy{PolicyPrimitive::IDLE};
    uint32_t current_hop{0};
    float residual_free_energy{0.0f};
    float delta_free_energy{0.0f};
    float hop_pragmatic_cost{0.0f};
    bool should_continue_foraging{false};
};

/**
 * Slot extraction metadata for computing Epistemic Slot Entropy H_slot
 */
struct SlotEntropyContext {
    float intent_confidence{1.0f};
    uint32_t total_required_slots{0};
    uint32_t missing_required_slots{0};

    float computeEntropy() const {
        constexpr float w_conf = 0.4f;
        constexpr float w_missing = 0.6f;
        float missing_ratio = (total_required_slots > 0)
            ? (static_cast<float>(missing_required_slots) / static_cast<float>(total_required_slots))
            : 0.0f;
        return w_conf * (1.0f - intent_confidence) + w_missing * missing_ratio;
    }
};

/**
 * Detailed evaluation outcome for a single candidate policy
 */
struct PolicyEvaluation {
    PolicyPrimitive policy{PolicyPrimitive::IDLE};
    float expected_free_energy{0.0f};  // G(pi)
    float pragmatic_cost{0.0f};
    float epistemic_value{0.0f};
    float selection_probability{0.0f}; // Softmax probability P(pi)
};

/**
 * Complete decision result returned by the Expected Free Energy Router
 */
struct PolicyDecision {
    PolicyPrimitive optimal_policy{PolicyPrimitive::IDLE};
    float min_efe{0.0f};
    float slot_entropy{0.0f};
    bool requires_clarification{false};
    std::array<PolicyEvaluation, 6> candidate_evaluations{};
};

/**
 * Level 2 Tactical Intent & Policy Engine:
 * Closed-form, single-step lookahead (T=1) Expected Free Energy (EFE) Evaluator.
 * Evaluates K=6 finite candidate policies:
 * { IDLE, SAMPLE_HIRES, CLARIFY_USER, INSPECT_DOC, EXEC_TOOL, SELF_HEAL }
 */
class ExpectedFreeEnergyRouter {
public:
    explicit ExpectedFreeEnergyRouter(BaselineProfile baseline = BaselineProfile{});

    void setBaseline(const BaselineProfile& baseline);
    const BaselineProfile& getBaseline() const;

    /**
     * Evaluates all K=6 candidate policies given:
     * - Current State s
     * - Prior Preferences C (target homeostatic state)
     * - Precisions Pi
     * - Realtime Variances sigma_prior^2
     * - Slot Entropy Context
     */
    PolicyDecision evaluate(
        const StateVector& current_state,
        const StateVector& prior_preferences,
        const std::array<float, MODALITY_COUNT>& precisions,
        const std::array<float, MODALITY_COUNT>& realtime_variances,
        const SlotEntropyContext& slot_ctx,
        float action_precision_gamma = 1.0f
    ) const;

    /**
     * Helper to compute slot entropy from user query directly using NLP slot extractors
     */
    static SlotEntropyContext analyzeQuery(const std::string& query, const std::string& route_name, float route_confidence);

    /**
     * Phase 1: Evaluates Dual-Gate BM25 Entropy for /docsearch Smart EmbeddingGemma 2 Wakeup.
     * - Temperature-scaled Softmax over top-K |bm25| scores
     * - Normalized Shannon Entropy H_hat in [0, 1]
     * - Selects LEXICAL_ONLY iff (|s_1| > theta_min && H_hat < entropy_max), else INSPECT_DOC.
     */
    static RagWakeupDecision evaluateRagWakeup(
        const std::vector<float>& bm25_scores,
        float temperature = 1.0f,
        float theta_min = 0.5f,
        float entropy_max = 0.35f,
        float resource_index = 1.0f
    );

    /**
     * Phase 2: Evaluates Local (Gemma 4 E2B) vs Cloud Escalation using:
     * - Pre-Inference Fast Prior Uncertainty H_prior (token length, reasoning keywords)
     * - Speculative 8-Token Probe Entropy H_probe
     * - Privacy Cost Injection (pii_risk_flag > 0 blocks cloud escalation via +100.0 cost)
     */
    static BrainRouteDecision evaluateBrainRoute(
        const std::string& query,
        float resource_index = 1.0f,
        float pii_risk_flag = 0.0f,
        float early_probe_entropy = 0.0f
    );

    /**
     * Phase 2: Evaluates Multi-Hop Research Foraging (/research) with:
     * - Quadratic Hop Cost C_hop(k) = c0 * k^2
     * - Diminishing Returns Early-Stop: Delta F = F_{k-1} - F_k < epsilon
     */
    static ResearchHopDecision evaluateResearchHop(
        uint32_t current_hop,
        float coverage_ratio,
        float prev_free_energy = 1.0f,
        float c0 = 0.25f,
        uint32_t max_hops = 2,
        float min_delta_f = 0.12f
    );

    static constexpr float THRESHOLD_EPISTEMIC = 0.45f;
    static constexpr size_t POLICY_COUNT = 6;
    static const char* getPolicyName(PolicyPrimitive policy);

private:
    BaselineProfile m_baseline;
};

} // namespace Ronin::Kernel::Reasoning
