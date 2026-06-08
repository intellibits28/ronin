#pragma once

#include <string>
#include <vector>
#include <memory>
#include "ronin_types.hpp"
#include "long_term_memory.h"
#include "thompson_sampler.h"

namespace Ronin::Kernel::Reasoning {

/**
 * v13.0 Reflection Engine: The Bayesian "Self-Correction" Loop.
 * Analyzes performance vs expectation and applies RLHF (Human Feedback).
 */
class ReflectionEngine {
public:
    ReflectionEngine(Memory::LongTermMemory* ltm, ThompsonSampler* sampler);

    // Callback to trigger weight updates in the main graph
    void setWeightUpdateCallback(std::function<void(const std::string&, bool)> cb) {
        m_weight_update_cb = cb;
    }

    /**
     * RLHF: Processes manual user feedback on a task.
     * Updates Bayesian counts for the selected path.
     */
    void applyHumanFeedback(const std::string& session_id, bool was_helpful);

    /**
     * Auto-Reflection: Analyzes completed episodes and updates beliefs.
     */
    void reflectOnRecentTasks();

    /**
     * Calculates semantic error score between predicted and actual outcomes.
     */
    float evaluateOutcome(const std::string& predicted, const std::string& actual);

private:
    Memory::LongTermMemory* m_ltm;
    ThompsonSampler* m_sampler;
    std::function<void(const std::string&, bool)> m_weight_update_cb;
};

} // namespace Ronin::Kernel::Reasoning
