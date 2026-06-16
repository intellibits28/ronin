#include "adaptive_budget_controller.h"
#include "failure_telemetry_bus.h"
#include "ronin_log.h"
#include <algorithm>

#define TAG "RoninBudget"

namespace Ronin::Kernel::Execution {

AdaptiveBudgetController& AdaptiveBudgetController::getInstance() {
    static AdaptiveBudgetController instance;
    return instance;
}

void AdaptiveBudgetController::updateWorldState(const Ronin::Kernel::WorldState& state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_world_state = state;
}

uint32_t AdaptiveBudgetController::getAdaptedBudget(const std::string& exec_id, const std::string& node_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    int recent_failures = FailureTelemetryBus::getInstance().getFailureCount(node_id);
    
    // v1.6 Phase 4: Context-Aware Execution Scaling
    float multiplier = 1.0f;
    std::string context_reason = "Normal";

    // 1. Failure-based scaling (v1.5) - Give a bit more time if struggling
    multiplier += std::min(recent_failures * 0.05f, 0.20f);

    // 2. Battery-based constraint (v1.6)
    if (m_world_state.battery_percent > 0.0f && m_world_state.battery_percent < 15.0f && !m_world_state.charging) {
        multiplier *= 0.7f; // 30% reduction to save power
        context_reason = "Low Battery";
    }

    // 3. Time-of-day contextual scaling (v1.6)
    // Between midnight and 6 AM, give agent maximum time to reflect and think deeply
    if (m_world_state.hour_of_day >= 0 && m_world_state.hour_of_day <= 5) {
        multiplier *= 1.5f; // 50% boost during idle night hours
        context_reason = "Night/Idle";
    }

    uint32_t final_budget = static_cast<uint32_t>(BASELINE_BUDGET_MS * multiplier);
    
    // Hard caps
    if (final_budget > 30000) final_budget = 30000;
    if (final_budget < 5000) final_budget = 5000;

    LOGI(TAG, "[%s] Allocated %d ms for %s (Context: %s, Multiplier: %.2f)", 
         exec_id.c_str(), final_budget, node_id.c_str(), context_reason.c_str(), multiplier);

    return final_budget;
}

void AdaptiveBudgetController::reportExecution(const std::string& node_id, uint32_t latency_ms, bool success) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& stats = m_node_history[node_id];
    
    if (success) {
        stats.success_count++;
        stats.avg_latency = (stats.avg_latency == 0) ? latency_ms : (stats.avg_latency * 0.9 + latency_ms * 0.1);
    } else {
        stats.failure_count++;
    }
}

} // namespace Ronin::Kernel::Execution
