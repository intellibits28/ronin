#include "reasoning/policy_router.hpp"
#include "intent_slot_extractors.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Ronin::Kernel::Reasoning {

ExpectedFreeEnergyRouter::ExpectedFreeEnergyRouter(BaselineProfile baseline)
    : m_baseline(baseline) {}

void ExpectedFreeEnergyRouter::setBaseline(const BaselineProfile& baseline) {
    m_baseline = baseline;
}

const BaselineProfile& ExpectedFreeEnergyRouter::getBaseline() const {
    return m_baseline;
}

const char* ExpectedFreeEnergyRouter::getPolicyName(PolicyPrimitive policy) {
    switch (policy) {
        case PolicyPrimitive::IDLE: return "IDLE";
        case PolicyPrimitive::SAMPLE_HIRES: return "SAMPLE_HIRES";
        case PolicyPrimitive::CLARIFY_USER: return "CLARIFY_USER";
        case PolicyPrimitive::INSPECT_DOC: return "INSPECT_DOC";
        case PolicyPrimitive::EXEC_TOOL: return "EXEC_TOOL";
        case PolicyPrimitive::SELF_HEAL: return "SELF_HEAL";
        default: return "UNKNOWN";
    }
}

SlotEntropyContext ExpectedFreeEnergyRouter::analyzeQuery(
    const std::string& query,
    const std::string& route_name,
    float route_confidence
) {
    SlotEntropyContext ctx;
    ctx.intent_confidence = std::clamp(route_confidence, 0.0f, 1.0f);

    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    // 1. File Share / Transfer query ("ဖိုင်ပို့ပေးပါ", "share file", "send file")
    bool is_file_send = (q.find("ဖိုင်") != std::string::npos && q.find("ပို့") != std::string::npos) ||
                        (q.find("send") != std::string::npos && q.find("file") != std::string::npos) ||
                        (q.find("share") != std::string::npos && q.find("file") != std::string::npos);

    if (is_file_send) {
        auto sms = Intent::IntentSlotExtractors::extractSmsSlots(query);
        auto file = Intent::IntentSlotExtractors::extractFileSearchSlots(query);
        ctx.total_required_slots = 2; // Required: recipient and file target
        ctx.missing_required_slots = 0;
        if (sms.recipient.empty()) {
            ctx.missing_required_slots++;
        }
        bool has_file_target = !file.file_extension.empty() || 
                               (!file.query.empty() && 
                                file.query != "ပို့" && 
                                file.query != "ပို့ပေး" && 
                                file.query != "ပို့ပေးပါ" &&
                                file.query.find("ပို့") == std::string::npos &&
                                file.query.find("send") == std::string::npos &&
                                file.query.find("share") == std::string::npos);
        if (!has_file_target) {
            ctx.missing_required_slots++;
        }
        return ctx;
    }

    if (route_name == "SEND_SMS") {
        auto sms = Intent::IntentSlotExtractors::extractSmsSlots(query);
        ctx.total_required_slots = 1; // Recipient is strictly required
        ctx.missing_required_slots = sms.recipient.empty() ? 1 : 0;
    } else if (route_name == "SET_ALARM") {
        auto alarm = Intent::IntentSlotExtractors::extractAlarmSlots(query);
        ctx.total_required_slots = 1; // Target wake time is strictly required
        ctx.missing_required_slots = alarm.time_str.empty() ? 1 : 0;
    } else if (route_name == "FILE_SEARCH") {
        auto file = Intent::IntentSlotExtractors::extractFileSearchSlots(query);
        ctx.total_required_slots = 1;
        ctx.missing_required_slots = file.is_valid ? 0 : 1;
    } else if (route_name == "VAULT_MEMORY") {
        auto vault = Intent::IntentSlotExtractors::extractVaultSlots(query);
        ctx.total_required_slots = 1;
        ctx.missing_required_slots = vault.is_valid ? 0 : 1;
    } else {
        // Self-contained commands (FLASHLIGHT, LOCATION, SHM, etc.)
        ctx.total_required_slots = 0;
        ctx.missing_required_slots = 0;
    }

    return ctx;
}

PolicyDecision ExpectedFreeEnergyRouter::evaluate(
    const StateVector& current_state,
    const StateVector& prior_preferences,
    const std::array<float, MODALITY_COUNT>& precisions,
    const std::array<float, MODALITY_COUNT>& realtime_variances,
    const SlotEntropyContext& slot_ctx,
    float action_precision_gamma
) const {
    PolicyDecision decision;
    float slot_entropy = slot_ctx.computeEntropy();
    decision.slot_entropy = slot_entropy;
    decision.requires_clarification = (slot_entropy > THRESHOLD_EPISTEMIC);

    // Baseline noise floors: an action cannot reduce variance below physical baseline
    const auto& base = m_baseline;
    std::array<float, MODALITY_COUNT> baseline_vars = {
        std::max(1e-6f, base.sigma_shm_hz * base.sigma_shm_hz),
        std::max(1e-6f, base.sigma_resource * base.sigma_resource),
        std::max(1e-6f, base.sigma_intent * base.sigma_intent),
        std::max(1e-6f, base.sigma_noise * base.sigma_noise)
    };

    constexpr float EPS = 1e-6f;

    std::array<PolicyPrimitive, POLICY_COUNT> policies = {
        PolicyPrimitive::IDLE,
        PolicyPrimitive::SAMPLE_HIRES,
        PolicyPrimitive::CLARIFY_USER,
        PolicyPrimitive::INSPECT_DOC,
        PolicyPrimitive::EXEC_TOOL,
        PolicyPrimitive::SELF_HEAL
    };

    float min_efe = std::numeric_limits<float>::infinity();
    size_t best_idx = 0;

    for (size_t k = 0; k < POLICY_COUNT; ++k) {
        PolicyPrimitive p = policies[k];
        PolicyEvaluation eval;
        eval.policy = p;

        std::array<float, MODALITY_COUNT> expected_obs{};
        std::array<float, MODALITY_COUNT> post_var = realtime_variances;
        float pragmatic_penalty = 0.0f;

        switch (p) {
            case PolicyPrimitive::IDLE: {
                expected_obs[0] = current_state.shm_freq_hz;
                expected_obs[1] = current_state.resource_index;
                expected_obs[2] = current_state.intent_certainty;
                expected_obs[3] = current_state.env_disturbance;
                // If there is an unresolved intent or active request, staying idle is penalized
                if (slot_ctx.intent_confidence > 0.5f && slot_entropy <= THRESHOLD_EPISTEMIC) {
                    pragmatic_penalty += precisions[2] * 0.5f;
                }
                break;
            }
            case PolicyPrimitive::SAMPLE_HIRES: {
                expected_obs[0] = current_state.shm_freq_hz;
                expected_obs[1] = std::max(0.0f, current_state.resource_index - 0.05f); // transient 200Hz battery draw
                expected_obs[2] = current_state.intent_certainty;
                expected_obs[3] = current_state.env_disturbance;
                // Reduces modal uncertainty down to baseline
                post_var[0] = std::max(baseline_vars[0], realtime_variances[0] * 0.10f);

                // If modal variance is already at baseline and frequency matches prior, sampling is unnecessary
                if (realtime_variances[0] <= baseline_vars[0] * 1.5f && 
                    std::abs(current_state.shm_freq_hz - prior_preferences.shm_freq_hz) < 0.2f) {
                    pragmatic_penalty += 1.0f;
                }
                break;
            }
            case PolicyPrimitive::CLARIFY_USER: {
                expected_obs[0] = current_state.shm_freq_hz;
                expected_obs[1] = std::max(0.0f, current_state.resource_index - 0.01f);
                expected_obs[2] = 1.0f; // User clarification resolves intent
                expected_obs[3] = current_state.env_disturbance;

                if (slot_entropy > THRESHOLD_EPISTEMIC) {
                    // Valid clarification: epistemic gain resolves intent uncertainty
                    post_var[2] = std::max(baseline_vars[2], realtime_variances[2] * 0.10f);
                } else {
                    // Unnecessary clarification on already-complete intent causes conversational friction
                    post_var[2] = realtime_variances[2];
                    float friction_delta = THRESHOLD_EPISTEMIC - slot_entropy + 0.3f;
                    pragmatic_penalty += precisions[2] * friction_delta * friction_delta + 2.0f;
                }
                break;
            }
            case PolicyPrimitive::INSPECT_DOC: {
                expected_obs[0] = current_state.shm_freq_hz;
                expected_obs[1] = std::max(0.0f, current_state.resource_index - 0.03f);
                expected_obs[2] = current_state.intent_certainty;
                expected_obs[3] = current_state.env_disturbance;

                // Inspect doc only provides epistemic gain when query requires document inspection
                pragmatic_penalty += 2.0f;
                break;
            }
            case PolicyPrimitive::EXEC_TOOL: {
                expected_obs[0] = current_state.shm_freq_hz;
                expected_obs[1] = std::max(0.0f, current_state.resource_index - 0.02f);
                expected_obs[3] = current_state.env_disturbance;

                if (slot_ctx.intent_confidence < 0.5f) {
                    // No active intent command: executing a tool is inappropriate
                    expected_obs[2] = current_state.intent_certainty;
                    pragmatic_penalty += 2.0f;
                } else if (slot_entropy <= THRESHOLD_EPISTEMIC) {
                    expected_obs[2] = 1.0f; // Successful tool satisfaction
                } else {
                    // Executing with missing slots fails
                    expected_obs[2] = std::max(0.0f, 1.0f - slot_entropy);
                    pragmatic_penalty += precisions[2] * 4.0f * (slot_entropy - THRESHOLD_EPISTEMIC);
                }
                break;
            }
            case PolicyPrimitive::SELF_HEAL: {
                expected_obs[0] = current_state.shm_freq_hz;
                expected_obs[1] = std::min(1.0f, current_state.resource_index + 0.35f);
                expected_obs[2] = current_state.intent_certainty;
                expected_obs[3] = current_state.env_disturbance; // Healing device does not stop external physical noise

                if (current_state.resource_index < 0.60f) {
                    // Epistemic stabilization of device state
                    post_var[1] = std::max(baseline_vars[1], realtime_variances[1] * 0.20f);
                } else {
                    // Unnecessary self-heal on healthy system
                    post_var[1] = realtime_variances[1];
                    float over_heal = current_state.resource_index - 0.60f;
                    pragmatic_penalty += precisions[1] * (over_heal * over_heal) + 2.0f;
                }
                break;
            }
        }

        // Pragmatic Cost = sum_i Pi_i * (E[o_i] - C_i)^2 + pragmatic_penalty
        float pragmatic_cost = pragmatic_penalty;
        for (size_t i = 0; i < MODALITY_COUNT; ++i) {
            float diff = expected_obs[i] - prior_preferences[i];
            pragmatic_cost += precisions[i] * (diff * diff);
        }

        // Epistemic Value = 0.5 * sum_i ln(sigma^2_prior / sigma^2_post)
        float epistemic_value = 0.0f;
        for (size_t i = 0; i < MODALITY_COUNT; ++i) {
            float prior_v = std::max(EPS, realtime_variances[i]);
            float post_v = std::max(EPS, post_var[i]);
            if (prior_v > post_v) {
                epistemic_value += 0.5f * std::log(prior_v / post_v);
            }
        }

        // Expected Free Energy G(pi) = Pragmatic Cost - Epistemic Value
        float efe = pragmatic_cost - epistemic_value;

        eval.pragmatic_cost = pragmatic_cost;
        eval.epistemic_value = epistemic_value;
        eval.expected_free_energy = efe;

        if (efe < min_efe) {
            min_efe = efe;
            best_idx = k;
        }

        decision.candidate_evaluations[k] = eval;
    }

    decision.optimal_policy = policies[best_idx];
    decision.min_efe = min_efe;

    // Softmax Distribution: P(pi) = exp(-gamma * (G(pi) - G_min)) / Z
    float z_sum = 0.0f;
    std::array<float, POLICY_COUNT> weights{};
    for (size_t k = 0; k < POLICY_COUNT; ++k) {
        float rel_g = decision.candidate_evaluations[k].expected_free_energy - min_efe;
        weights[k] = std::exp(-action_precision_gamma * rel_g);
        z_sum += weights[k];
    }

    float inv_z = (z_sum > EPS) ? (1.0f / z_sum) : (1.0f / static_cast<float>(POLICY_COUNT));
    for (size_t k = 0; k < POLICY_COUNT; ++k) {
        decision.candidate_evaluations[k].selection_probability = weights[k] * inv_z;
    }

    return decision;
}

} // namespace Ronin::Kernel::Reasoning
