#include "long_term_memory.h"
#include <cmath>
#include <ctime>
#include <vector>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <sstream>
#include "ronin_log.h"

#define TAG "RoninLongTermMemory"

namespace Ronin::Kernel::Memory {

LongTermMemory::LongTermMemory(const std::string& db_path) 
    : m_segmenter(std::make_unique<Ronin::Kernel::NLP::MyanmarSegmenter>()) {
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
    // v13.0 Migration: Drop legacy facts table if it exists (checks for old 'key' column)
    sqlite3_stmt* check_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT key FROM facts LIMIT 1;", -1, &check_stmt, nullptr) == SQLITE_OK) {
        sqlite3_finalize(check_stmt);
        sqlite3_exec(m_db, "DROP TABLE facts;", nullptr, nullptr, nullptr);
        LOGI(TAG, "v13.0 Migration: Dropped legacy facts table.");
    }

    std::vector<const char*> statements = {
        "CREATE TABLE IF NOT EXISTS notes (id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT, content TEXT, tags TEXT, created_at INTEGER, updated_at INTEGER);",
        "CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(title, content, content='notes', content_rowid='id');",
        "CREATE TABLE IF NOT EXISTS facts (id INTEGER PRIMARY KEY AUTOINCREMENT, entity TEXT, attribute TEXT, value TEXT, source_type INTEGER DEFAULT 0, confidence REAL DEFAULT 1.0, last_verified_at INTEGER, created_at INTEGER, updated_at INTEGER);",
        "CREATE INDEX IF NOT EXISTS idx_facts_lookup ON facts(entity, attribute);",
        "CREATE TABLE IF NOT EXISTS vault (id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT, encrypted_blob TEXT, created_at INTEGER);",
        "CREATE TABLE IF NOT EXISTS episodes (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER, intent TEXT, goal_id TEXT, node_id TEXT, summary TEXT, payload_json TEXT, outcome_enum INTEGER, latency_ms INTEGER, confidence_before REAL, confidence_after REAL);",
        "CREATE VIRTUAL TABLE IF NOT EXISTS episodes_fts USING fts5(summary, content='episodes', content_rowid='id');",
        "CREATE TABLE IF NOT EXISTS predictions (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER, goal_id TEXT, node_id TEXT, predicted_json TEXT, actual_json TEXT, error_score REAL);",
        "CREATE TABLE IF NOT EXISTS chat_history (id INTEGER PRIMARY KEY AUTOINCREMENT, role TEXT, content TEXT, timestamp INTEGER);",
        "CREATE VIRTUAL TABLE IF NOT EXISTS file_index USING fts5(name, path, extension, last_modified UNINDEXED);",
        "CREATE TABLE IF NOT EXISTS audit (id INTEGER PRIMARY KEY AUTOINCREMENT, action TEXT, details TEXT, timestamp INTEGER);"
    };

    for (const char* sql : statements) {
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            LOGE(TAG, "Schema error on [%s]: %s", sql, sqlite3_errmsg(m_db));
            // Don't return false yet, try to create as much as possible
        }
    }

    // FTS5 Triggers
    sqlite3_exec(m_db, "CREATE TRIGGER IF NOT EXISTS notes_ai AFTER INSERT ON notes BEGIN INSERT INTO notes_fts(rowid, title, content) VALUES (new.id, new.title, new.content); END;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "CREATE TRIGGER IF NOT EXISTS episodes_ai AFTER INSERT ON episodes BEGIN INSERT INTO episodes_fts(rowid, summary) VALUES (new.id, new.summary); END;", nullptr, nullptr, nullptr);
    
    return true;
}

bool LongTermMemory::storeNote(const std::string& title, const std::string& content, const std::string& tags) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO notes (title, content, tags, created_at, updated_at) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    uint64_t now = std::time(nullptr);
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, tags.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, now);
    sqlite3_bind_int64(stmt, 5, now);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool LongTermMemory::storeFact(const std::string& entity, const std::string& attr, const std::string& value, SourceType source, float confidence) {
    if (!m_db) { LOGE(TAG, "storeFact failed: DB is null."); return false; }
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO facts (entity, attribute, value, source_type, confidence, last_verified_at, created_at, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    uint64_t now = std::time(nullptr);
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE(TAG, "storeFact prepare failed: %s", sqlite3_errmsg(m_db));
        return false;
    }
    sqlite3_bind_text(stmt, 1, entity.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, attr.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, value.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, static_cast<int>(source));
    sqlite3_bind_double(stmt, 5, confidence);
    sqlite3_bind_int64(stmt, 6, now);
    sqlite3_bind_int64(stmt, 7, now);
    sqlite3_bind_int64(stmt, 8, now);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!success) { LOGE(TAG, "storeFact step failed: %s", sqlite3_errmsg(m_db)); }
    sqlite3_finalize(stmt);
    return success;
}

bool LongTermMemory::storeVault(const std::string& title, const std::string& encrypted_blob) {
    if (!m_db) { LOGE(TAG, "storeVault failed: DB is null."); return false; }
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO vault (title, encrypted_blob, created_at) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, encrypted_blob.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, std::time(nullptr));
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!success) { LOGE(TAG, "storeVault step failed: %s", sqlite3_errmsg(m_db)); }
        sqlite3_finalize(stmt);
        return success;
    } else {
        LOGE(TAG, "storeVault prepare failed: %s", sqlite3_errmsg(m_db));
    }
    return false;
}

std::string LongTermMemory::lookupVault(const std::string& title) {
    if (!m_db) return "";
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT encrypted_blob FROM vault WHERE title LIKE ? ORDER BY created_at DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    std::string result;
    std::string title_query = "%" + title + "%";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, title_query.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            if (val) result = reinterpret_cast<const char*>(val);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

bool LongTermMemory::storeEpisode(const std::string& intent, const std::string& summary, const std::string& payload_json, 
                                  bool success, const std::string& goal_id, const std::string& node_id,
                                  int64_t latency_ms, float conf_before, float conf_after) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO episodes (timestamp, intent, goal_id, node_id, summary, payload_json, outcome_enum, latency_ms, confidence_before, confidence_after) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, std::time(nullptr));
        sqlite3_bind_text(stmt, 2, intent.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, goal_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, node_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, summary.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, payload_json.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 7, success ? 1 : 0);
        sqlite3_bind_int64(stmt, 8, latency_ms);
        sqlite3_bind_double(stmt, 9, conf_before);
        sqlite3_bind_double(stmt, 10, conf_after);
        bool res = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return res;
    }
    return false;
}

bool LongTermMemory::storePrediction(const std::string& goal_id, const std::string& node_id, 
                                     const std::string& predicted_json, const std::string& actual_json, float error_score) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO predictions (timestamp, goal_id, node_id, predicted_json, actual_json, error_score) "
                      "VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, std::time(nullptr));
        sqlite3_bind_text(stmt, 2, goal_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, node_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, predicted_json.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, actual_json.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 6, error_score);
        bool res = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return res;
    }
    return false;
}

std::string LongTermMemory::lookupFact(const std::string& entity, const std::string& attr) {
    if (!m_db) return "";
    std::lock_guard<std::mutex> lock(m_mutex);
    // v10.2.13: Use LIKE for entity to handle Myanmar suffixes (ရဲ့, ၏, က)
    // v12.15: Use LIKE for attribute as well for fuzzy matching
    const char* sql = "SELECT value FROM facts WHERE entity LIKE ? AND attribute LIKE ? ORDER BY confidence DESC, created_at DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    std::string result;
    std::string entity_query = "%" + entity + "%";
    std::string attr_query = "%" + attr + "%";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, entity_query.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, attr_query.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            if (val) result = reinterpret_cast<const char*>(val);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> LongTermMemory::searchNotes(const std::string& query) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    const char* sql = "SELECT title, content FROM notes_fts WHERE notes_fts MATCH ? ORDER BY rank LIMIT 5;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            results.push_back(title + ": " + content);
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::string> LongTermMemory::searchEpisodes(const std::string& query) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    const char* sql = "SELECT summary FROM episodes_fts WHERE episodes_fts MATCH ? ORDER BY rank LIMIT 5;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* summary = sqlite3_column_text(stmt, 0);
            if (summary) results.push_back(reinterpret_cast<const char*>(summary));
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::string> LongTermMemory::getNotesList() {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT title FROM notes ORDER BY updated_at DESC LIMIT 20;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::pair<std::string, std::string>> LongTermMemory::getFactsList() {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::pair<std::string, std::string>> results;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT entity, attribute || ' -> ' || value FROM facts ORDER BY created_at DESC LIMIT 20;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back({
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))
            });
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

bool LongTermMemory::storeMessage(const std::string& role, const std::string& content, int64_t timestamp) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO chat_history (role, content, timestamp) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, role.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, (timestamp == 0) ? std::time(nullptr) : timestamp);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> LongTermMemory::getHistory(int limit, int offset) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::pair<std::string, std::string>> history;
    const char* sql = "SELECT role, content FROM chat_history ORDER BY id DESC LIMIT ? OFFSET ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        sqlite3_bind_int(stmt, 2, offset);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            history.push_back({
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))
            });
        }
    }
    sqlite3_finalize(stmt);
    return history;
}

bool LongTermMemory::clearHistory() {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    return sqlite3_exec(m_db, "DELETE FROM chat_history;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool LongTermMemory::loadSegmenter(const std::string& dict_path) {
    return m_segmenter && m_segmenter->loadDictionary(dict_path);
}

std::vector<std::string> LongTermMemory::segmentText(const std::string& input) {
    if (!m_segmenter) return {};
    std::string segmented = m_segmenter->segment(input);
    std::vector<std::string> tokens;
    std::stringstream ss(segmented);
    std::string t;
    while (ss >> t) tokens.push_back(t);
    return tokens;
}

bool LongTermMemory::indexFile(const std::string& name, const std::string& path, const std::string& ext, uint64_t modified) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT OR REPLACE INTO file_index (name, path, extension, last_modified) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, ext.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(modified));
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

std::vector<std::string> LongTermMemory::searchFiles(const std::string& query) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    std::string like_query = "%" + query + "%";
    const char* sql = "SELECT path FROM file_index WHERE name LIKE ? OR path LIKE ? LIMIT 10;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, like_query.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, like_query.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::string> LongTermMemory::search(const std::string& query) { return searchNotes(query); }
bool LongTermMemory::consolidate(const std::string& summary) { return storeNote("Consolidated Summary", summary, "auto"); }
void LongTermMemory::applyDecay(uint64_t) {}
int LongTermMemory::runMaintenance(bool) { return 0; }
bool LongTermMemory::storeAuditLog(const std::string& action, const std::string& details) { return true; }

std::vector<LongTermMemory::EpisodeRecord> LongTermMemory::getRecentFailures(int limit) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<EpisodeRecord> results;
    const char* sql = "SELECT intent, summary, outcome_enum, timestamp FROM episodes WHERE outcome_enum = 0 ORDER BY timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            EpisodeRecord rec;
            rec.intent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            rec.summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            rec.success = false;
            rec.timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
            results.push_back(rec);
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

} // namespace Ronin::Kernel::Memory
