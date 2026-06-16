#include "failure_telemetry_bus.h"
#include "ronin_log.h"
#include <chrono>

#define TAG "FailureTelemetry"

namespace Ronin::Kernel::Execution {

FailureTelemetryBus& FailureTelemetryBus::getInstance() {
    static FailureTelemetryBus instance;
    return instance;
}

void FailureTelemetryBus::logFailure(const std::string& exec_id, const std::string& node_id, FailureType type, const std::string& details) {
    LOGW(TAG, "[SEMANTIC FAILURE] ExecID: %s | Node: %s | Type: %d | Details: %s", 
         exec_id.c_str(), node_id.c_str(), static_cast<int>(type), details.c_str());
         
    if (!m_ltm) return;
    m_ltm->storeFailure(node_id, static_cast<int>(type), 0, details);
}

std::vector<Ronin::Kernel::FailureRecord> FailureTelemetryBus::getRecentFailures(const std::string& node_id, int limit) {
    if (!m_ltm) return {};
    return m_ltm->getFailuresByNode(node_id, limit);
}

int FailureTelemetryBus::getFailureCount(const std::string& node_id, uint64_t window_ms) {
    if (!m_ltm) return 0;
    uint64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000000;
    uint64_t since = now - window_ms;
    return m_ltm->countFailures(node_id, since);
}

} // namespace Ronin::Kernel::Execution
