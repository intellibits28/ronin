#include "reflection_engine.h"
#include <cmath>
#include "ronin_log.h"

#define TAG "RoninReflection"

namespace Ronin::Kernel::Reasoning {

ReflectionEngine::ReflectionEngine(Memory::LongTermMemory* ltm, ThompsonSampler* sampler, Model::InferenceEngine* engine)
    : m_ltm(ltm), m_sampler(sampler), m_engine(engine) {}

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
    
    if (recent_episodes.empty() && semantic_failures.empty()) {
        LOGI(TAG, "Reflection: No new episodes to analyze. Cycle skipped.");
        return;
    }

    // 2. Synthesize Lessons (LLM-driven Evolutionary Planning)
    if (m_engine && !semantic_failures.empty()) {
        std::string context = "Recent Agent Failures:\n";
        for (const auto& fail : semantic_failures) {
            context += "- Intent: " + fail.intent + ". Summary: " + fail.summary + "\n";
        }
        
        std::string prompt = 
            "Analyze the following failures and write a single, concise behavioral rule (1 sentence) to prevent the agent from repeating these mistakes. "
            "Output ONLY the rule.\n" + context;
            
        std::string lesson = m_engine->runLiteRTReasoning("", prompt);
        
        // Sanitize LLM output
        if (!lesson.empty() && lesson.find("Error") == std::string::npos && lesson.find("Status Code") == std::string::npos) {
            LOGI(TAG, "Reflection: Derived new lesson: %s", lesson.c_str());
            m_ltm->storeNote("Nightly Lesson", lesson, "lesson");
        } else {
            LOGW(TAG, "Reflection: LLM failed to synthesize a valid lesson.");
        }
    } else if (!semantic_failures.empty()) {
        // Fallback static analysis if LLM is unavailable
        for (const auto& fail : semantic_failures) {
            m_ltm->storeNote("Nightly Lesson", "Intent '" + fail.intent + "' failed recently. " + fail.summary, "lesson");
            LOGI(TAG, "Reflection: Generated static lesson for %s", fail.intent.c_str());
        }
    }

    // 3. Memory Consolidation
    std::unordered_map<std::string, int> success_map;
    for (const auto& ep : recent_episodes) {
        if (ep.success) success_map[ep.intent]++;
    }

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
