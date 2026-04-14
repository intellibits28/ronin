#include "long_term_memory.h"
#include <cmath>
#include <ctime>
#include <vector>
#include <iostream>
#include <algorithm>
#include "ronin_log.h"

#define TAG "RoninLongTermMemory"

namespace Ronin::Kernel::Memory {

LongTermMemory::LongTermMemory(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &m_db) != SQLITE_OK) {
        LOGE(TAG, "Failed to open SQLite database: %s", sqlite3_errmsg(m_db));
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
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
        ");"
        
        "CREATE TABLE IF NOT EXISTS chat_history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  role TEXT, "
        "  content TEXT, "
        "  timestamp INTEGER);"
        
        "CREATE VIRTUAL TABLE IF NOT EXISTS file_index USING fts5("
        "  name, "
        "  path, "
        "  extension, "
        "  last_modified UNINDEXED, "
        "  embedding_vector UNINDEXED"
        ");";

    if (sqlite3_exec(m_db, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to create SQLite schema: %s", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

bool LongTermMemory::storeFact(const std::string& key, const std::string& value, MemoryPriority priority) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT OR REPLACE INTO facts (key, value, last_accessed, priority) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    
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

    const char* select_sql = "SELECT key, stability, last_accessed FROM facts WHERE priority = 0;";
    sqlite3_stmt* stmt = nullptr;
    std::vector<std::string> keys_to_prune;

    if (sqlite3_prepare_v2(m_db, select_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* key_ptr = sqlite3_column_text(stmt, 0);
            if (!key_ptr) continue;

            std::string key = reinterpret_cast<const char*>(key_ptr);
            double initial_stability = sqlite3_column_double(stmt, 1);
            uint64_t last_accessed = sqlite3_column_int64(stmt, 2);

            double current_stability;
            if (current_time > last_accessed) {
                double delta_t = static_cast<double>(current_time - last_accessed);
                current_stability = initial_stability * std::exp(-m_lambda * delta_t);
            } else {
                current_stability = initial_stability;
            }

            if (current_stability < 0.1) {
                keys_to_prune.push_back(key);
            }
        }
    }
    sqlite3_finalize(stmt);

    if (!keys_to_prune.empty()) {
        const char* delete_sql = "DELETE FROM facts WHERE key = ?;";
        sqlite3_stmt* del_stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, delete_sql, -1, &del_stmt, nullptr) == SQLITE_OK) {
            sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
            for (const auto& key : keys_to_prune) {
                sqlite3_bind_text(del_stmt, 1, key.c_str(), -1, SQLITE_STATIC);
                if (sqlite3_step(del_stmt) == SQLITE_DONE) {
                    pruned_count++;
                }
                sqlite3_reset(del_stmt);
            }
            sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
        }
        sqlite3_finalize(del_stmt);
    }

    if (pruned_count > 0) {
        LOGI(TAG, "Natural Forgetting complete: %d items cleared from Deep-store.", pruned_count);
    }
    return pruned_count;
}

std::string LongTermMemory::retrieveFact(const std::string& key) {
    if (!m_db) return "";
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT value, stability, last_accessed FROM facts WHERE key = ?;";
    sqlite3_stmt* stmt = nullptr;
    std::string result = "";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val_ptr = sqlite3_column_text(stmt, 0);
            if (val_ptr) {
                result = reinterpret_cast<const char*>(val_ptr);
            }
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> LongTermMemory::search(const std::string& query) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    const char* sql = "SELECT content FROM summaries WHERE summaries MATCH ? ORDER BY rank;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* content_ptr = sqlite3_column_text(stmt, 0);
            if (content_ptr) {
                results.push_back(reinterpret_cast<const char*>(content_ptr));
            }
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

bool LongTermMemory::consolidate(const std::string& summary_text) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO summaries (content, timestamp) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, summary_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, std::time(nullptr));
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool LongTermMemory::indexFile(const std::string& name, const std::string& path, const std::string& ext, uint64_t modified, const std::vector<float>& embedding) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT OR REPLACE INTO file_index (name, path, extension, last_modified, embedding_vector) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, ext.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(modified));

    if (!embedding.empty()) {
        sqlite3_bind_blob(stmt, 5, embedding.data(), static_cast<int>(embedding.size() * sizeof(float)), SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 5);
    }

    bool success = sqlite3_step(stmt) == SQLITE_DONE; // Wait, it's SQLITE_DONE
    sqlite3_finalize(stmt);
    return success;
}

std::vector<LongTermMemory::FileEmbedding> LongTermMemory::getAllFileEmbeddings() {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<FileEmbedding> results;

    const char* sql = "SELECT name, embedding_vector FROM file_index WHERE embedding_vector IS NOT NULL;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* name = sqlite3_column_text(stmt, 0);
            const void* blob = sqlite3_column_blob(stmt, 1);
            int bytes = sqlite3_column_bytes(stmt, 1);

            if (name && blob && bytes > 0) {
                FileEmbedding fe;
                fe.name = reinterpret_cast<const char*>(name);
                fe.vector.assign(static_cast<const float*>(blob), static_cast<const float*>(blob) + (bytes / sizeof(float)));
                results.push_back(fe);
            }
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::string> LongTermMemory::searchFiles(const std::string& query) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    
    // Using LIKE for flexible partial match since FTS5 MATCH doesn't support leading wildcards easily
    std::string like_query = "%" + query + "%";
    const char* sql = "SELECT name FROM file_index WHERE name LIKE ? LIMIT 10;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, like_query.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* name = sqlite3_column_text(stmt, 0);
            if (name) {
                results.push_back(reinterpret_cast<const char*>(name));
            }
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

void LongTermMemory::applyDecay(uint64_t current_timestamp) {
    if (!m_db) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    LOGI(TAG, "Applying database-wide Temporal Decay...");
    
    const char* select_sql = "SELECT key, stability, last_accessed FROM facts WHERE priority < 3;";
    sqlite3_stmt* stmt = nullptr;
    
    struct UpdateEntry { std::string key; double new_stability; };
    std::vector<UpdateEntry> updates;

    if (sqlite3_prepare_v2(m_db, select_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* key_ptr = sqlite3_column_text(stmt, 0);
            if (!key_ptr) continue;

            std::string key = reinterpret_cast<const char*>(key_ptr);
            double initial_stability = sqlite3_column_double(stmt, 1);
            uint64_t last_accessed = sqlite3_column_int64(stmt, 2);

            double new_stability;
            if (current_timestamp > last_accessed) {
                double delta_t = static_cast<double>(current_timestamp - last_accessed);
                new_stability = initial_stability * std::exp(-m_lambda * delta_t);
            } else {
                new_stability = initial_stability;
            }
            
            updates.push_back({key, new_stability});
        }
    }
    sqlite3_finalize(stmt);

    const char* up_sql = "UPDATE facts SET stability = ?, last_accessed = ? WHERE key = ?;";
    sqlite3_stmt* up_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, up_sql, -1, &up_stmt, nullptr) == SQLITE_OK) {
        sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        for (const auto& entry : updates) {
            sqlite3_bind_double(up_stmt, 1, entry.new_stability);
            sqlite3_bind_int64(up_stmt, 2, current_timestamp);
            sqlite3_bind_text(up_stmt, 3, entry.key.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(up_stmt);
            sqlite3_reset(up_stmt);
        }
        sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
        sqlite3_finalize(up_stmt);
    }
}

bool LongTermMemory::storeMessage(const std::string& role, const std::string& content) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO chat_history (role, content, timestamp) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, role.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, std::time(nullptr));
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

std::vector<std::pair<std::string, std::string>> LongTermMemory::getHistory(int limit) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::pair<std::string, std::string>> history;
    const char* sql = "SELECT role, content FROM chat_history ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* role = sqlite3_column_text(stmt, 0);
            const unsigned char* content = sqlite3_column_text(stmt, 1);
            if (role && content) {
                history.push_back({reinterpret_cast<const char*>(role), reinterpret_cast<const char*>(content)});
            }
        }
    }
    sqlite3_finalize(stmt);
    // Reverse to get chronological order
    std::reverse(history.begin(), history.end());
    return history;
}

} // namespace Ronin::Kernel::Memory
