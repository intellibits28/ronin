#pragma once
#include <string>
#include <memory>
#include <sqlite3.h>
#include <mutex>
#include "execution_context.h"

namespace Ronin::Kernel::Execution {

class ExecutionCheckpointStore {
public:
    static ExecutionCheckpointStore& getInstance();
    void initialize(sqlite3* db);

    bool saveCheckpoint(ExecutionContextPtr ctx, const std::string& graph_state_json);
    std::string loadCheckpoint(const std::string& exec_id);
    bool deleteCheckpoint(const std::string& exec_id);
    std::vector<std::string> getPendingExecutions();

private:
    ExecutionCheckpointStore() = default;
    sqlite3* m_db = nullptr;
    std::mutex m_mutex;
    bool initSchema();
};

} // namespace Ronin::Kernel::Execution
