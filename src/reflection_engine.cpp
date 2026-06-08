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
    // Phase 3.4: Auto-reflect logic
    // 1. Query failed episodes from LTM
    // 2. Identify common failure patterns (e.g. timeout, empty results)
    // 3. Update BeliefState to avoid these paths in future prompts
}

float ReflectionEngine::evaluateOutcome(const std::string& predicted, const std::string& actual) {
    if (predicted == actual) return 0.0f;
    if (actual.empty() || actual.find("Error") != std::string::npos) return 1.0f;
    
    // Simple heuristic: distance in string length or keyword overlap
    // Phase 3.5: Implement TF-IDF or Embedding distance here
    return 0.5f; 
}

} // namespace Ronin::Kernel::Reasoning
