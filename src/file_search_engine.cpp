#include "file_search_engine.h"
#include "ronin_log.h"
#include <iostream>

#define TAG "RoninFileSearch"

namespace Ronin::Kernel::Capability {

FileSearchEngine::FileSearchEngine(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &m_db) != SQLITE_OK) {
        LOGE(TAG, "Failed to open FileSearch database: %s", sqlite3_errmsg(m_db));
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    } else {
        initSchema();
    }
}

FileSearchEngine::~FileSearchEngine() {
    if (m_db) sqlite3_close(m_db);
}

bool FileSearchEngine::initSchema() {
    const char* schema = 
        "CREATE VIRTUAL TABLE IF NOT EXISTS file_index USING fts5("
        "  name, path UNINDEXED);";

    if (sqlite3_exec(m_db, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to initialize FTS5 schema: %s", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

bool FileSearchEngine::indexFile(const std::string& filename, const std::string& path) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql = "INSERT OR REPLACE INTO file_index (name, path) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, filename.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_STATIC);
    
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

std::vector<std::string> FileSearchEngine::searchFiles(const std::string& query) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;

    const char* sql = "SELECT name, path FROM file_index WHERE file_index MATCH ? ORDER BY rank;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* name = sqlite3_column_text(stmt, 0);
            const unsigned char* path = sqlite3_column_text(stmt, 1);
            if (name && path) {
                results.push_back(reinterpret_cast<const char*>(name));
            }
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

} // namespace Ronin::Kernel::Capability
