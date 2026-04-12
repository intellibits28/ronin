#include "long_term_memory.h"
#include <cmath>
#include <ctime>
#include <iostream>
#include "ronin_log.h"

#define TAG "RoninLongTermMemory"

namespace Ronin::Kernel::Memory {

LongTermMemory::LongTermMemory(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &m_db) != SQLITE_OK) {
        LOGE(TAG, "Failed to open SQLite database: %s", sqlite3_errmsg(m_db));
    } else {
        initSchema();
    }
}

LongTermMemory::~LongTermMemory() {
    if (m_db) sqlite3_close(m_db);
}

bool LongTermMemory::initSchema() {
    const char* schema = 
        "CREATE TABLE IF NOT EXISTS facts ("
        "  key TEXT PRIMARY KEY, "
        "  value TEXT, "
        "  stability REAL DEFAULT 1.0, "
        "  last_accessed INTEGER, "
        "  priority INTEGER DEFAULT 1);"
        
        "CREATE VIRTUAL TABLE IF NOT EXISTS summaries USING fts5("
        "  content, "
        "  timestamp UNINDEXED"
        ");";

    if (sqlite3_exec(m_db, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to create SQLite schema: %s", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

bool LongTermMemory::storeFact(const std::string& key, const std::string& value, MemoryPriority priority) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT OR REPLACE INTO facts (key, value, last_accessed, priority) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, std::time(nullptr));
    sqlite3_bind_int(stmt, 4, static_cast<int>(priority));
    
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

int LongTermMemory::runMaintenance(bool is_charging) {
    if (!is_charging || !m_db) {
        LOGI(TAG, "Maintenance skipped: Device not charging or DB not open.");
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    LOGI(TAG, "Natural Forgetting: Scanning L3 Deep-store for stale memories...");

    uint64_t current_time = std::time(nullptr);
    int pruned_count = 0;

    const char* select_sql = "SELECT key, stability, last_accessed, priority FROM facts WHERE priority = 0;";
    sqlite3_stmt* stmt;
    std::vector<std::string> keys_to_prune;

    if (sqlite3_prepare_v2(m_db, select_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            double initial_stability = sqlite3_column_double(stmt, 1);
            uint64_t last_accessed = sqlite3_column_int64(stmt, 2);

            double delta_t = static_cast<double>(current_time - last_accessed);
            double current_stability = initial_stability * std::exp(-m_lambda * delta_t);

            if (current_stability < 0.1) {
                keys_to_prune.push_back(key);
            }
        }
    }
    sqlite3_finalize(stmt);

    for (const auto& key : keys_to_prune) {
        const char* delete_sql = "DELETE FROM facts WHERE key = ?;";
        sqlite3_stmt* del_stmt;
        if (sqlite3_prepare_v2(m_db, delete_sql, -1, &del_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(del_stmt, 1, key.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(del_stmt) == SQLITE_DONE) {
                pruned_count++;
                LOGI(TAG, "Natural Forgetting: Pruned stale memory '%s' (Stability < 0.1)", key.c_str());
            }
        }
        sqlite3_finalize(del_stmt);
    }

    if (pruned_count > 0) {
        LOGI(TAG, "Natural Forgetting complete: %d items cleared from Deep-store.", pruned_count);
    }
    return pruned_count;
}

std::string LongTermMemory::retrieveFact(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT value, stability, last_accessed FROM facts WHERE key = ?;";
    sqlite3_stmt* stmt;
    std::string result = "";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> LongTermMemory::search(const std::string& query) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    const char* sql = "SELECT content FROM summaries WHERE summaries MATCH ? ORDER BY rank;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

bool LongTermMemory::consolidate(const std::string& summary_text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO summaries (content, timestamp) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, summary_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, std::time(nullptr));
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

void LongTermMemory::applyDecay(uint64_t current_timestamp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    LOGI(TAG, "Applying database-wide Temporal Decay...");
    
    // We update stability for all non-critical facts
    // SQLite doesn't have exp() natively, so we select and update.
    const char* select_sql = "SELECT key, stability, last_accessed FROM facts WHERE priority < 3;";
    sqlite3_stmt* stmt;
    
    struct UpdateEntry { std::string key; double new_stability; };
    std::vector<UpdateEntry> updates;

    if (sqlite3_prepare_v2(m_db, select_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            double initial_stability = sqlite3_column_double(stmt, 1);
            uint64_t last_accessed = sqlite3_column_int64(stmt, 2);

            double delta_t = static_cast<double>(current_timestamp - last_accessed);
            double new_stability = initial_stability * std::exp(-m_lambda * delta_t);
            
            updates.push_back({key, new_stability});
        }
    }
    sqlite3_finalize(stmt);

    for (const auto& entry : updates) {
        const char* up_sql = "UPDATE facts SET stability = ?, last_accessed = ? WHERE key = ?;";
        sqlite3_stmt* up_stmt;
        if (sqlite3_prepare_v2(m_db, up_sql, -1, &up_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_double(up_stmt, 1, entry.new_stability);
            sqlite3_bind_int64(up_stmt, 2, current_timestamp);
            sqlite3_bind_text(up_stmt, 3, entry.key.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(up_stmt);
        }
        sqlite3_finalize(up_stmt);
    }
}

} // namespace Ronin::Kernel::Memory
y
emory
