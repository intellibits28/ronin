#pragma once
#include <string>
#include <vector>
#include "ronin_types.hpp"
#include "long_term_memory.h"

namespace Ronin::Kernel::Execution {

/**
 * v10.7: FailureTelemetryStore - Learning loop for runtime failures.
 */
class FailureTelemetryStore {
public:
    static FailureTelemetryStore& getInstance();
    
    void setMemory(Memory::LongTermMemory* ltm) { m_ltm = ltm; }
    
    bool recordFailure(const std::string& node_id, FailureType type, int retry_count, const std::string& resolution);
    std::vector<FailureRecord> getRecentFailures(int limit = 10);
    float getFailureRate(const std::string& node_id, uint64_t window_ms = 3600000);

private:
    FailureTelemetryStore() = default;
    Memory::LongTermMemory* m_ltm = nullptr;
};

} // namespace Ronin::Kernel::Execution
