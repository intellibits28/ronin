#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <nlohmann/json.hpp>

namespace Ronin::Kernel::Reasoning {

/**
 * PlannerRuleCache caches generated plans for intent strings to avoid recomputation.
 * Thread‑safe LRU cache with configurable max size.
 */
class PlannerRuleCache {
public:
    explicit PlannerRuleCache(size_t maxEntries = 256);
    // Retrieve cached plan JSON if present
    std::optional<std::string> get(const std::string &intent) const;
    // Store plan JSON for intent
    void put(const std::string &intent, const std::string &planJson);
    // Clear cache (e.g., on policy change)
    void clear();
private:
    mutable std::mutex m_mutex;
    size_t m_maxEntries;
    std::unordered_map<std::string, std::string> m_cache;
};

} // namespace Ronin::Kernel::Reasoning
