#include "execution_checkpoint_store.h"
#include "ronin_log.h"

#define TAG "ExecutionCheckpointStore"

namespace Ronin::Kernel::Execution {

ExecutionCheckpointStore& ExecutionCheckpointStore::getInstance() {
    static ExecutionCheckpointStore instance;
    return instance;
}

void ExecutionCheckpointStore::initialize(sqlite3* db) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_db = db;
    initSchema();
}

bool ExecutionCheckpointStore::initSchema() {
    if (!m_db) return false;
    const char* sql = "CREATE TABLE IF NOT EXISTS execution_checkpoints ("
                      "exec_id TEXT PRIMARY KEY, "
                      "session_id TEXT, "
                      "correlation_id TEXT, "
                      "graph_state_json TEXT, "
                      "timestamp INTEGER);";
    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGE(TAG, "Schema creation failed: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool ExecutionCheckpointStore::saveCheckpoint(ExecutionContextPtr ctx, const std::string& graph_state_json) {
    if (!m_db || !ctx) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql = "INSERT OR REPLACE INTO execution_checkpoints (exec_id, session_id, correlation_id, graph_state_json, timestamp) "
                      "VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, ctx->execution_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ctx->session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, ctx->correlation_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, graph_state_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, std::time(nullptr));

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::string ExecutionCheckpointStore::loadCheckpoint(const std::string& exec_id) {
    if (!m_db) return "";
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql = "SELECT graph_state_json FROM execution_checkpoints WHERE exec_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    std::string result = "";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, exec_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            if (val) result = reinterpret_cast<const char*>(val);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

bool ExecutionCheckpointStore::deleteCheckpoint(const std::string& exec_id) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql = "DELETE FROM execution_checkpoints WHERE exec_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, exec_id.c_str(), -1, SQLITE_TRANSIENT);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::vector<std::string> ExecutionCheckpointStore::getPendingExecutions() {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<std::string> results;
    const char* sql = "SELECT exec_id FROM execution_checkpoints;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            if (val) results.push_back(reinterpret_cast<const char*>(val));
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

} // namespace Ronin::Kernel::Execution
