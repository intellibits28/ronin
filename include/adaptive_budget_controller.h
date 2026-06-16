#pragma once
#include <string>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include "ronin_types.hpp"

namespace Ronin::Kernel::Execution {

class AdaptiveBudgetController {
public:
    static AdaptiveBudgetController& getInstance();
    
    uint32_t getAdaptedBudget(const std::string& exec_id, const std::string& node_id);
    void reportExecution(const std::string& node_id, uint32_t latency_ms, bool success);
    void updateWorldState(const Ronin::Kernel::WorldState& state);

private:
    AdaptiveBudgetController() = default;
    std::mutex m_mutex;
    Ronin::Kernel::WorldState m_world_state;
    
    struct NodeStats {
        uint32_t avg_latency = 0;
        int failure_count = 0;
        int success_count = 0;
    };
    
    std::unordered_map<std::string, NodeStats> m_node_history;
    const uint32_t BASELINE_BUDGET_MS = 15000;
};

} // namespace Ronin::Kernel::Execution
