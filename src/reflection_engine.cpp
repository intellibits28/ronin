#include "reflection_engine.h"
#include <cmath>
#include "ronin_log.h"

#define TAG "RoninReflection"

namespace Ronin::Kernel::Reasoning {

ReflectionEngine::ReflectionEngine(Memory::LongTermMemory* ltm, ThompsonSampler* sampler)
    : m_ltm(ltm), m_sampler(sampler) {}

void ReflectionEngine::applyHumanFeedback(const std::string& session_id, bool was_helpful) {
    LOGI(TAG, "RLHF: Received manual feedback for session %s. Helpful: %d", session_id.c_str(), was_helpful);
    
    // Phase 3.3: Trigger graph executor weight update if callback is configured
    if (m_weight_update_cb) {
        m_weight_update_cb(session_id, was_helpful);
    }
}

void ReflectionEngine::reflectOnRecentTasks() {
    // Phase 4: Self-Evaluation Loop
    LOGI(TAG, "Initiating Self-Evaluation Loop...");
    
    if (!m_ltm) return;

    // 1. Query failed episodes from LTM
    auto failures = m_ltm->getRecentFailures(10);
    
    // 2. Identify common failure patterns
    std::unordered_map<std::string, int> intent_failures;
    for (const auto& f : failures) {
        intent_failures[f.intent]++;
    }

    // 3. Update BeliefState to avoid these paths (e.g. mark reliability)
    for (const auto& [intent, count] : intent_failures) {
        if (count >= 3) {
            float reliability = 1.0f - (static_cast<float>(count) / 10.0f);
            LOGW(TAG, "Self-Evaluation: Intent %s has low reliability (%.2f)", intent.c_str(), reliability);
            
            // Phase 3 Integration: Store reliability belief
            // We use a specific key format so TaskPlanner can see it
            // m_belief_state.updateBelief("reliability_" + intent, std::to_string(reliability), 0.8f);
            // Note: Since ReflectionEngine doesn't have direct access to BeliefState yet, 
            // we log it for now. In a full implementation, we'd add the reference.
        }
    }
    
    LOGI(TAG, "Self-Evaluation Loop Completed.");
}

float ReflectionEngine::evaluateOutcome(const std::string& predicted, const std::string& actual) {
    if (predicted == actual) return 0.0f;
    if (actual.empty() || actual.find("Error") != std::string::npos) return 1.0f;
    
    // Simple heuristic: distance in string length or keyword overlap
    // Phase 3.5: Implement TF-IDF or Embedding distance here
    return 0.5f; 
}

} // namespace Ronin::Kernel::Reasoning
