#include "execution_budget.h"
#include "execution_context.h"
#include "ronin_log.h"
#include <chrono>

#define TAG "ExecutionGovernance"

namespace Ronin::Kernel::Execution {

ExecutionTelemetryBus& ExecutionTelemetryBus::getInstance() {
    static ExecutionTelemetryBus instance;
    return instance;
}

void ExecutionTelemetryBus::logNodeStart(const std::string& session_id, const std::string& exec_id, const std::string& node_id) {
    LOGI(TAG, "[%s | %s | NODE:%s] Node Execution Started", session_id.c_str(), exec_id.c_str(), node_id.c_str());
}

void ExecutionTelemetryBus::logNodeEnd(const std::string& session_id, const std::string& exec_id, const std::string& node_id, int64_t latency_ms, const std::string& result, int budget_consumed) {
    LOGI(TAG, "[%s | %s | NODE:%s] Node Execution Ended | Latency: %lld ms | Budget Used: %d ms | Result: %s", 
         session_id.c_str(), exec_id.c_str(), node_id.c_str(), (long long)latency_ms, budget_consumed, result.c_str());
}

void ExecutionTelemetryBus::logCancellation(const std::string& session_id, const std::string& exec_id, const std::string& reason) {
    LOGW(TAG, "[%s | %s | CANCEL] %s", session_id.c_str(), exec_id.c_str(), reason.c_str());
}

void ExecutionTelemetryBus::logGatewayHop(const std::string& session_id, const std::string& exec_id, const std::string& direction) {
    LOGI(TAG, "[%s | %s | JNI_HOP] %s", session_id.c_str(), exec_id.c_str(), direction.c_str());
}

ExecutionBudgetController& ExecutionBudgetController::getInstance() {
    static ExecutionBudgetController instance;
    return instance;
}

void ExecutionBudgetController::allocateBudget(const std::string& exec_id, uint32_t budget_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_budgets[exec_id] = budget_ms;
    LOGI(TAG, "[BUDGET] Allocated %u ms for execution: %s", budget_ms, exec_id.c_str());
}

bool ExecutionBudgetController::consumeBudget(const std::string& exec_id, uint32_t cost_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_budgets.find(exec_id);
    if (it != m_budgets.end()) {
        if (it->second >= cost_ms) {
            it->second -= cost_ms;
            return true;
        } else {
            LOGE(TAG, "[BUDGET_EXHAUSTED] Execution %s ran out of budget. Needed %u, had %u", exec_id.c_str(), cost_ms, it->second);
            it->second = 0;
            return false;
        }
    }
    LOGW(TAG, "[BUDGET_UNKNOWN] Execution %s consumed %u ms without budget allocation.", exec_id.c_str(), cost_ms);
    return false; // Implicitly fail if no budget allocated
}

uint32_t ExecutionBudgetController::getRemaining(const std::string& exec_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_budgets.find(exec_id);
    return (it != m_budgets.end()) ? it->second : 0;
}

void ExecutionBudgetController::revokeBudget(const std::string& exec_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_budgets.erase(exec_id);
}

} // namespace Ronin::Kernel::Execution
