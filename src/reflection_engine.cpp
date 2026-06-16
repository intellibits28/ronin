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
    LOGI(TAG, ">>> INITIATING NIGHTLY REFLECTION CYCLE <<<");
    
    if (!m_ltm) return;

    // 1. Gather historical context
    auto recent_episodes = m_ltm->getRecentEpisodes(20);
    auto semantic_failures = m_ltm->getRecentFailures(10);
    
    if (recent_episodes.empty()) {
        LOGI(TAG, "Reflection: No new episodes to analyze. Cycle skipped.");
        return;
    }

    // 2. Synthesize Lessons (Episode Synthesis)
    std::unordered_map<std::string, int> success_map;
    std::unordered_map<std::string, int> failure_map;
    
    for (const auto& ep : recent_episodes) {
        if (ep.success) success_map[ep.intent]++;
        else failure_map[ep.intent]++;
    }

    // 3. Behavioral Adjustment (Policy Evolution)
    for (auto const& [intent, fail_count] : failure_map) {
        int total = fail_count + success_map[intent];
        float failure_rate = static_cast<float>(fail_count) / static_cast<float>(total);
        
        if (failure_rate > 0.4f && total >= 3) {
            LOGW(TAG, "Reflection: Intent '%s' is highly unstable (Failure Rate: %.2f). Applying Policy Constraint.", 
                 intent.c_str(), failure_rate);
            
            // v1.6: In a full implementation, this would update the 'policies' table 
            // or inject a system-level constraint to prefer fallback tools for this intent.
            m_ltm->storeNote("Nightly Lesson", "Intent '" + intent + "' is unreliable. Use cautious planning.", "lesson");
        }
    }

    // 4. Memory Consolidation
    if (recent_episodes.size() >= 5) {
        std::string consolidated_brief = "Recent activity analysis: ";
        for (auto const& [intent, count] : success_map) {
            consolidated_brief += intent + "(" + std::to_string(count) + " successful), ";
        }
        m_ltm->consolidate(consolidated_brief);
        LOGI(TAG, "Reflection: Episodic memory consolidated.");
    }
    
    LOGI(TAG, ">>> NIGHTLY REFLECTION CYCLE COMPLETE <<<");
}

float ReflectionEngine::evaluateOutcome(const std::string& predicted, const std::string& actual) {
    if (predicted == actual) return 0.0f;
    if (actual.empty() || actual.find("Error") != std::string::npos) return 1.0f;
    
    // Simple heuristic: distance in string length or keyword overlap
    // Phase 3.5: Implement TF-IDF or Embedding distance here
    return 0.5f; 
}

} // namespace Ronin::Kernel::Reasoning
