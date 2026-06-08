#include "belief_state.h"
#include <chrono>
#include "ronin_log.h"

#define TAG "RoninBelief"

namespace Ronin::Kernel::Reasoning {

BeliefState::BeliefState(Memory::LongTermMemory* ltm) : m_ltm(ltm) {}

void BeliefState::updateBelief(const std::string& key, const std::string& value, float confidence) {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t now = std::chrono::system_clock::now().time_since_epoch().count();
    
    m_beliefs[key] = {key, value, confidence, now};
    
    // Auto-persist high confidence beliefs to LTM as Facts
    if (m_ltm && confidence > 0.8f) {
        m_ltm->storeFact("System", key, value, Memory::SourceType::BELIEF, confidence);
    }
}

Belief BeliefState::getBelief(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_beliefs.count(key)) return m_beliefs[key];
    return {key, "", 0.0f, 0};
}

void BeliefState::synchronize() {
    // Phase 3.2: Load critical world-facts from LTM into working beliefs
    // This will be expanded once we have a dedicated Fact Search
}

void BeliefState::clearTransient() {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Remove beliefs with low confidence or marked as transient
    for (auto it = m_beliefs.begin(); it != m_beliefs.end(); ) {
        if (it->second.confidence < 0.3f) it = m_beliefs.erase(it);
        else ++it;
    }
}

} // namespace Ronin::Kernel::Reasoning
