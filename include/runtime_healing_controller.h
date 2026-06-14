#pragma once
#include <string>
#include <memory>
#include <atomic>
#include "execution_context.h"
#include "execution_checkpoint_store.h"
#include "failure_telemetry_bus.h"
#include "agent_scheduler.h"

namespace Ronin::Kernel::Execution {

class RuntimeHealingController {
public:
    static RuntimeHealingController& getInstance();

    void initialize(AgentScheduler* scheduler);

    // Watchdog and detection
    void detectInstability();
    
    // Recovery actions
    void triggerSafeModeRecovery();
    bool rehydrateSession(const std::string& exec_id);
    void purgeDeadQueues();

private:
    RuntimeHealingController() = default;
    
    AgentScheduler* m_scheduler = nullptr;
    std::atomic<bool> m_is_healing{false};
    
    // Thresholds
    const int MAX_JNI_FAILURES_PER_MIN = 3;
    const int MAX_TIMEOUTS_PER_MIN = 5;
};

} // namespace Ronin::Kernel::Execution
