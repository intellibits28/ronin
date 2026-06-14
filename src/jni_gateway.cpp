#include "jni_gateway.h"
#include "jni_utils.h"
#include "execution_budget.h"
#include "execution_checkpoint_store.h"
#include "adaptive_budget_controller.h"
#include "ronin_log.h"
#include "agent_scheduler.h"

#define TAG "JniExecutionGateway"

namespace Ronin::Kernel::JNI {

JniExecutionGateway& JniExecutionGateway::getInstance() {
    static JniExecutionGateway instance;
    return instance;
}

KernelResult<Execution::ExecutionContextPtr> JniExecutionGateway::createAndValidateContext(
    JNIEnv* env, jstring jSessionId, jstring jExecId, jstring jCorrelationId) {
    
    std::string session_id = ConvertJStringToString(env, jSessionId);
    std::string exec_id = ConvertJStringToString(env, jExecId);
    std::string corr_id = ConvertJStringToString(env, jCorrelationId);

    if (session_id.empty() || exec_id.empty()) {
        return KernelResult<Execution::ExecutionContextPtr>::Error(400, "Invalid execution context identifiers.");
    }

    Execution::ExecutionTelemetryBus::getInstance().logGatewayHop(session_id, exec_id, "KOTLIN -> JNI -> C++");

    auto ctx = std::make_shared<Execution::ExecutionContext>();
    ctx->session_id = session_id;
    ctx->execution_id = exec_id;
    ctx->correlation_id = corr_id;

    // v1.5: Recovery Checkpoint
    Execution::ExecutionCheckpointStore::getInstance().saveCheckpoint(ctx, "{}");

    // Allocate default 15s budget (adapted)
    uint32_t budget = Execution::AdaptiveBudgetController::getInstance().getAdaptedBudget(exec_id, "gateway");
    Execution::ExecutionBudgetController::getInstance().allocateBudget(exec_id, budget);

    registerExecution(ctx);

    return KernelResult<Execution::ExecutionContextPtr>::Success(ctx);
}

void JniExecutionGateway::propagateCancellation(const std::string& exec_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_active_executions.find(exec_id);
    if (it != m_active_executions.end()) {
        LOGW(TAG, "Cross-Language Cancellation Triggered for ExecID: %s", exec_id.c_str());
        it->second->cancel_token->cancel();
        Execution::ExecutionTelemetryBus::getInstance().logCancellation(it->second->session_id, exec_id, "KOTLIN_COROUTINE_CANCELLED");
        
        // Also revoke budget to force stops on next tick
        Execution::ExecutionBudgetController::getInstance().revokeBudget(exec_id);
        m_active_executions.erase(it);
    }
}

void JniExecutionGateway::triggerSafeMode() {
    std::lock_guard<std::mutex> lock(m_mutex);
    LOGE(TAG, "Gateway SafeMode Triggered. Cancelling %zu active executions.", m_active_executions.size());
    for (auto& [exec_id, ctx] : m_active_executions) {
        ctx->cancel_token->cancel();
        Execution::ExecutionTelemetryBus::getInstance().logCancellation(ctx->session_id, exec_id, "SAFE_MODE_FORCED");
        Execution::ExecutionBudgetController::getInstance().revokeBudget(exec_id);
    }
    m_active_executions.clear();
}

void JniExecutionGateway::registerExecution(Execution::ExecutionContextPtr ctx) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active_executions[ctx->execution_id] = ctx;
}

void JniExecutionGateway::unregisterExecution(const std::string& exec_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active_executions.erase(exec_id);
    Execution::ExecutionBudgetController::getInstance().revokeBudget(exec_id);
}

Execution::ExecutionContextPtr JniExecutionGateway::getContext(const std::string& exec_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_active_executions.find(exec_id);
    return (it != m_active_executions.end()) ? it->second : nullptr;
}

} // namespace Ronin::Kernel::JNI
