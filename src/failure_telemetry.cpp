#include "failure_telemetry.h"
#include <chrono>

namespace Ronin::Kernel::Execution {

FailureTelemetryStore& FailureTelemetryStore::getInstance() {
    static FailureTelemetryStore instance;
    return instance;
}

bool FailureTelemetryStore::recordFailure(const std::string& node_id, FailureType type, int retry_count, const std::string& resolution) {
    if (!m_ltm) return false;
    return m_ltm->storeFailure(node_id, static_cast<int>(type), retry_count, resolution);
}

std::vector<FailureRecord> FailureTelemetryStore::getRecentFailures(int limit) {
    if (!m_ltm) return {};
    return m_ltm->getFailures(limit);
}

float FailureTelemetryStore::getFailureRate(const std::string& node_id, uint64_t window_ms) {
    if (!m_ltm) return 0.0f;
    uint64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000000;
    uint64_t since = now - window_ms;
    
    // Simple rate: count of failures in the window. 
    // In a more complex system, we'd divide by total attempts.
    // For now, we return count as a proxy for "risk".
    return static_cast<float>(m_ltm->countFailures(node_id, since));
}

} // namespace Ronin::Kernel::Execution
