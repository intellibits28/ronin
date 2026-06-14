#include "adaptive_budget_controller.h"
#include "failure_telemetry_bus.h"
#include <algorithm>

namespace Ronin::Kernel::Execution {

AdaptiveBudgetController& AdaptiveBudgetController::getInstance() {
    static AdaptiveBudgetController instance;
    return instance;
}

uint32_t AdaptiveBudgetController::getAdaptedBudget(const std::string& exec_id, const std::string& node_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    int recent_failures = FailureTelemetryBus::getInstance().getFailureCount(node_id);
    
    // v1.5 Adaptation Formula:
    // Base 15s. If failing frequently, back off the budget slightly (+20% max) to allow recovery
    // but if it's thrashing too much, we might want to reduce concurrency or budget.
    // For now: allow a bit more time if failing, capped at 18s.
    
    float multiplier = 1.0f + std::min(recent_failures * 0.05f, 0.20f);
    return static_cast<uint32_t>(BASELINE_BUDGET_MS * multiplier);
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
