#include "adaptive_budget.h"
#include "failure_telemetry.h"
#include <algorithm>

namespace Ronin::Kernel::Execution {

AdaptiveBudgetController& AdaptiveBudgetController::getInstance() {
    static AdaptiveBudgetController instance;
    return instance;
}

uint32_t AdaptiveBudgetController::getAdaptedBudget(const std::string& exec_id, const std::string& node_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    float risk_factor = FailureTelemetryStore::getInstance().getFailureRate(node_id);
    
    // v1.5 Adaptation Formula:
    // Budget = Baseline * (1.0 + (risk_factor * 0.05))
    // Max increase = 20% (+3000ms)
    
    float multiplier = 1.0f + std::min(risk_factor * 0.05f, 0.20f);
    return static_cast<uint32_t>(BASELINE_BUDGET_MS * multiplier);
}

void AdaptiveBudgetController::reportExecution(const std::string& node_id, uint32_t latency_ms, bool success) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& stats = m_node_history[node_id];
    
    if (success) {
        stats.success_count++;
        // Moving average
        stats.avg_latency = (stats.avg_latency == 0) ? latency_ms : (stats.avg_latency * 0.9 + latency_ms * 0.1);
    } else {
        stats.failure_count++;
    }
}

} // namespace Ronin::Kernel::Execution
