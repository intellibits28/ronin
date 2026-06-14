#pragma once
#include <string>
#include <vector>
#include "ronin_types.hpp"
#include "long_term_memory.h"

namespace Ronin::Kernel::Execution {

class FailureTelemetryBus {
public:
    static FailureTelemetryBus& getInstance();
    
    void setMemory(Memory::LongTermMemory* ltm) { m_ltm = ltm; }
    
    void logFailure(const std::string& exec_id, const std::string& node_id, FailureType type, const std::string& details);
    std::vector<FailureRecord> getRecentFailures(const std::string& node_id, int limit = 10);
    int getFailureCount(const std::string& node_id, uint64_t window_ms = 3600000);

private:
    FailureTelemetryBus() = default;
    Memory::LongTermMemory* m_ltm = nullptr;
};

} // namespace Ronin::Kernel::Execution
