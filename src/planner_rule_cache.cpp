#include "planner_rule_cache.h"
#include <list>

namespace Ronin::Kernel::Reasoning {

PlannerRuleCache::PlannerRuleCache(size_t maxEntries) : m_maxEntries(maxEntries) {}

std::optional<std::string> PlannerRuleCache::get(const std::string &intent) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cache.find(intent);
    if (it != m_cache.end()) {
        return it->second;
    }
    return std::nullopt;
}

void PlannerRuleCache::put(const std::string &intent, const std::string &planJson) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cache.size() >= m_maxEntries) {
        // Simple eviction: remove first inserted entry
        auto eraseIt = m_cache.begin();
        m_cache.erase(eraseIt);
    }
    m_cache[intent] = planJson;
}

void PlannerRuleCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

} // namespace Ronin::Kernel::Reasoning
