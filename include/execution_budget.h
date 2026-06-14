#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include "execution_context.h"

namespace Ronin::Kernel::Execution {

class ExecutionBudgetController {
public:
    static ExecutionBudgetController& getInstance();
    
    void allocateBudget(const std::string& exec_id, uint32_t budget_ms);
    bool consumeBudget(const std::string& exec_id, uint32_t cost_ms);
    uint32_t getRemaining(const std::string& exec_id);
    void revokeBudget(const std::string& exec_id);

private:
    ExecutionBudgetController() = default;
    std::mutex m_mutex;
    std::unordered_map<std::string, uint32_t> m_budgets;
};

} // namespace Ronin::Kernel::Execution
