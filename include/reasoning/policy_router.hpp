#pragma once

#include "reasoning/active_inference.hpp"
#include <array>
#include <string>
#include <string_view>
#include <cmath>

namespace Ronin::Kernel::Reasoning {

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

    static constexpr float THRESHOLD_EPISTEMIC = 0.45f;
    static constexpr size_t POLICY_COUNT = 6;
    static const char* getPolicyName(PolicyPrimitive policy);

private:
    BaselineProfile m_baseline;
};

} // namespace Ronin::Kernel::Reasoning
