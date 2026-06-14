#pragma once
#include <string>
#include <mutex>
#include <vector>
#include "execution_context.h"
#include "ronin_types.hpp"

namespace Ronin::Kernel::Execution {

/**
 * v10.7: RecoveryManager - Auto Crash and Exception recovery.
 */
class RecoveryManager {
public:
    static RecoveryManager& getInstance();
    
    bool recordCheckpoint(ExecutionContextPtr ctx);
    ExecutionContextPtr getLastValidContext();
    
    // v1.5 Recovery Strategy
    bool recoverFromFailure(const std::string& exec_id, FailureType type);

private:
    RecoveryManager() = default;
    std::mutex m_mutex;
    std::vector<ExecutionContextPtr> m_checkpoints;
};

} // namespace Ronin::Kernel::Execution
