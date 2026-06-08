#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "ronin_types.hpp"
#include "long_term_memory.h"

namespace Ronin::Kernel::Reasoning {

/**
 * v13.0 Belief State: Manages confidence-weighted truths about the world and user.
 * Serves as the "Working Memory" for the Bayesian Brain.
 */
class BeliefState {
public:
    BeliefState(Memory::LongTermMemory* ltm = nullptr);

    /**
     * Updates or creates a belief.
     * Automatically persists to LTM if confidence > threshold.
     */
    void updateBelief(const std::string& key, const std::string& value, float confidence = 1.0f);

    /**
     * Retrieves a belief by key.
     * Returns a neutral belief if not found.
     */
    Belief getBelief(const std::string& key);

    /**
     * Syncs beliefs from the LTM (Long-term Facts).
     */
    void synchronize();

    /**
     * Clears transient beliefs (Session Isolation).
     */
    void clearTransient();

private:
    std::unordered_map<std::string, Belief> m_beliefs;
    Memory::LongTermMemory* m_ltm;
    std::mutex m_mutex;
};

} // namespace Ronin::Kernel::Reasoning
