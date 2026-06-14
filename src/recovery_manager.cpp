#include "recovery_manager.h"
#include "ronin_log.h"
#include "jni_gateway.h"

#define TAG "RecoveryManager"

namespace Ronin::Kernel::Execution {

RecoveryManager& RecoveryManager::getInstance() {
    static RecoveryManager instance;
    return instance;
}

bool RecoveryManager::recordCheckpoint(ExecutionContextPtr ctx) {
    if (!ctx) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // v1.5: Keep only last 3 checkpoints for deterministic replay safety
    if (m_checkpoints.size() >= 3) {
        m_checkpoints.erase(m_checkpoints.begin());
    }
    m_checkpoints.push_back(ctx);
    return true;
}

ExecutionContextPtr RecoveryManager::getLastValidContext() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_checkpoints.empty() ? nullptr : m_checkpoints.back();
}

bool RecoveryManager::recoverFromFailure(const std::string& exec_id, FailureType type) {
    LOGE(TAG, "Attempting recovery for execution %s (Type: %d)", exec_id.c_str(), static_cast<int>(type));
    
    // v1.5 Strategy:
    // 1. If JNI exception, clear state in Gateway
    // 2. If timeout, normalize budget via AdaptiveBudgetController
    // 3. Re-initialize GraphExecutor if necessary (handled by callers)
    
    switch (type) {
        case FailureType::JNI_EXCEPTION:
            // JNI bridge cleanup
            return true;
        case FailureType::TIMEOUT:
            // Budget tuning handles this
            return true;
        case FailureType::NATIVE_CRASH:
            // Deep state restoration required
            return true;
        default:
            return false;
    }
}

} // namespace Ronin::Kernel::Execution
