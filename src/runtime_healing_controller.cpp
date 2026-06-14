#include "runtime_healing_controller.h"
#include "ronin_log.h"
#include <chrono>

#define TAG "RuntimeHealing"

namespace Ronin::Kernel::Execution {

RuntimeHealingController& RuntimeHealingController::getInstance() {
    static RuntimeHealingController instance;
    return instance;
}

void RuntimeHealingController::initialize(AgentScheduler* scheduler) {
    m_scheduler = scheduler;
    LOGI(TAG, "Healing Controller initialized.");
}

void RuntimeHealingController::detectInstability() {
    if (m_is_healing.exchange(true)) return;

    // v1.5 Detection logic:
    // Query FailureTelemetryBus for recent trends.
    // If consecutive failures exceed thresholds -> trigger recovery.
    
    // For now: Mock detection
    LOGI(TAG, "Running health check...");
    
    m_is_healing.store(false);
}

void RuntimeHealingController::triggerSafeModeRecovery() {
    LOGE(TAG, "Critical instability detected. Forcing SafeMode Recovery.");
    if (m_scheduler) m_scheduler->purgeQueue();
    // Implementation would also signal Kotlin UI to show recovery dialog.
}

bool RuntimeHealingController::rehydrateSession(const std::string& exec_id) {
    LOGI(TAG, "Attempting to rehydrate session: %s", exec_id.c_str());
    std::string state = ExecutionCheckpointStore::getInstance().loadCheckpoint(exec_id);
    if (state.empty()) {
        LOGW(TAG, "No checkpoint found for session rehydration.");
        return false;
    }
    // Re-queue the session with the restored state
    return true;
}

void RuntimeHealingController::purgeDeadQueues() {
    if (m_scheduler) m_scheduler->purgeQueue();
}

} // namespace Ronin::Kernel::Execution
