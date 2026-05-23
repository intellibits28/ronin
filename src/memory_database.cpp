#include "memory_database.h"
#include "ronin_log.h"
#include <ctime>

#define TAG "RoninMemoryDatabase"

namespace Ronin::Kernel::Data {

MemoryDatabase::MemoryDatabase(const std::string& db_path) {
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(db_path.c_str(), &m_db, flags, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to open database at %s: %s", db_path.c_str(), sqlite3_errmsg(m_db));
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    } else {
        if (!initSchema()) {
            LOGE(TAG, "Schema initialization failed.");
        }
    }
}

MemoryDatabase::~MemoryDatabase() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool MemoryDatabase::initSchema() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Base Table Creation (Spec v2.1)
    const char* create_base_table = 
        "CREATE TABLE IF NOT EXISTS memories ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  raw_text_mm TEXT NOT NULL,"
        "  segmented_text_mm TEXT NOT NULL,"
        "  state_enum INTEGER DEFAULT 0,"
        "  timestamp INTEGER NOT NULL,"
        "  source TEXT DEFAULT 'user'"
        ");";

    // 2. FTS5 Virtual Table Creation (Spec v2.1 - content_rowid='id')
    const char* create_fts_table = 
        "CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5("
        "  segmented_text_mm,"
        "  content='memories',"
        "  content_rowid='id'"
        ");";

    // 3. Triggers for FTS5 Sync & Ghost Data Prevention (Spec v2.1)
    const char* create_triggers = 
        // Sync on INSERT
        "CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN "
        "  INSERT INTO memories_fts(id, segmented_text_mm) VALUES (new.id, new.segmented_text_mm); "
        "END; "

        // Sync on DELETE
        "CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN "
        "  INSERT INTO memories_fts(memories_fts, id, segmented_text_mm) VALUES('delete', old.id, old.segmented_text_mm); "
        "END; "

        // Sync on UPDATE (Explicit DELETE + INSERT pattern to prevent Ghost Data)
        "CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN "
        "  INSERT INTO memories_fts(memories_fts, id, segmented_text_mm) VALUES('delete', old.id, old.segmented_text_mm); "
        "  INSERT INTO memories_fts(id, segmented_text_mm) VALUES (new.id, new.segmented_text_mm); "
        "END;";

    if (sqlite3_exec(m_db, create_base_table, nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    if (sqlite3_exec(m_db, create_fts_table, nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    if (sqlite3_exec(m_db, create_triggers, nullptr, nullptr, nullptr) != SQLITE_OK) return false;

    LOGI(TAG, "Ronin Memory Schema v2.1 initialized successfully.");
    return true;
}

bool MemoryDatabase::insertMemory(const std::string& raw_text, const std::string& segmented_text, MemoryState state, const std::string& source) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql = "INSERT INTO memories (raw_text_mm, segmented_text_mm, state_enum, timestamp, source) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, raw_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, segmented_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, static_cast<int>(state));
    sqlite3_bind_int64(stmt, 4, std::time(nullptr));
    sqlite3_bind_text(stmt, 5, source.c_str(), -1, SQLITE_STATIC);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool MemoryDatabase::updateMemoryState(int id, MemoryState new_state) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql = "UPDATE memories SET state_enum = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, static_cast<int>(new_state));
    sqlite3_bind_int(stmt, 2, id);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool MemoryDatabase::deleteMemory(int id) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql = "DELETE FROM memories WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, id);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

std::vector<MemoryEntry> MemoryDatabase::searchFTS(const std::string& query, int limit) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<MemoryEntry> results;
    // FTS5 BM25 Ranking with State Filtering (excluding Tombstoned)
    const char* sql = 
        "SELECT m.id, m.raw_text_mm, m.segmented_text_mm, m.state_enum, m.timestamp, m.source "
        "FROM memories m "
        "JOIN memories_fts f ON m.id = f.id "
        "WHERE memories_fts MATCH ? AND m.state_enum < 4 "
        "ORDER BY rank LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};

    sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryEntry entry;
        entry.id = sqlite3_column_int(stmt, 0);
        entry.raw_text_mm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.segmented_text_mm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry.state = static_cast<MemoryState>(sqlite3_column_int(stmt, 3));
        entry.timestamp = sqlite3_column_int64(stmt, 4);
        entry.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        results.push_back(entry);
    }

    sqlite3_finalize(stmt);
    return results;
}

int MemoryDatabase::purgeTombstoned() {
    if (!m_db) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql = "DELETE FROM memories WHERE state_enum = 4;";
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) return 0;

    return sqlite3_changes(m_db);
}

} // namespace Ronin::Kernel::Data
