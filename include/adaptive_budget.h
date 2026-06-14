#pragma once
#include <string>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace Ronin::Kernel::Execution {

/**
 * v10.7: AdaptiveBudgetController - Dynamically scales execution cost.
 */
class AdaptiveBudgetController {
public:
    static AdaptiveBudgetController& getInstance();
    
    uint32_t getAdaptedBudget(const std::string& exec_id, const std::string& node_id);
    void reportExecution(const std::string& node_id, uint32_t latency_ms, bool success);

private:
    AdaptiveBudgetController() = default;
    std::mutex m_mutex;
    
    struct NodeStats {
        uint32_t avg_latency = 0;
        int failure_count = 0;
        int success_count = 0;
    };
    
    std::unordered_map<std::string, NodeStats> m_node_history;
    const uint32_t BASELINE_BUDGET_MS = 15000;
};

} // namespace Ronin::Kernel::Execution
