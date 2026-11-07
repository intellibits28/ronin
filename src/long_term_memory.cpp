#include "long_term_memory.h"
#include <cmath>
#include <ctime>
#include <vector>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <unordered_set>
#include "ronin_log.h"

#define TAG "RoninLongTermMemory"

namespace Ronin::Kernel::Memory {

namespace {
constexpr int kCurrentSchemaVersion = 3;

std::string columnText(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? reinterpret_cast<const char*>(text) : "";
}

bool tableHasColumn(sqlite3* db, const char* table, const char* column) {
    std::string sql = "PRAGMA table_info(" + std::string(table) + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (columnText(stmt, 1) == column) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

std::string sanitizeFts5Query(const std::string& query) {
    std::string clean;
    clean.reserve(query.size() + 2);
    for (char c : query) {
        if (c == '"' || c == '\'' || c == '*' || c == '^' || c == '(' || c == ')' || c == ':' || c == '{' || c == '}') {
            clean += ' ';
        } else {
            clean += c;
        }
    }
    size_t start = clean.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = clean.find_last_not_of(" \t\r\n");
    std::string trimmed = clean.substr(start, end - start + 1);
    std::stringstream ss(trimmed);
    std::string token;
    std::string result;
    while (ss >> token) {
        if (!result.empty()) result += " ";
        result += "\"" + token + "\"";
    }
    return result;
}
}

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
    std::vector<const char*> statements = {
        "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);",
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
        "CREATE TABLE IF NOT EXISTS audit (id INTEGER PRIMARY KEY AUTOINCREMENT, action TEXT, details TEXT, timestamp INTEGER);",
        "CREATE TABLE IF NOT EXISTS failures (id INTEGER PRIMARY KEY AUTOINCREMENT, node_id TEXT, failure_type INTEGER, timestamp INTEGER, retry_count INTEGER, resolution TEXT);",
        "CREATE TABLE IF NOT EXISTS perception_history (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER, state_type TEXT, state_value TEXT);",
        "CREATE TABLE IF NOT EXISTS document_embeddings (id INTEGER PRIMARY KEY AUTOINCREMENT, doc_path TEXT NOT NULL, doc_hash TEXT NOT NULL DEFAULT '', chunk_index INTEGER NOT NULL, chunk_text TEXT NOT NULL, embedding_blob BLOB NOT NULL, model_id TEXT NOT NULL DEFAULT 'embeddinggemma-2-270m', dimension INTEGER NOT NULL, created_at INTEGER NOT NULL, UNIQUE(doc_path, model_id, chunk_index) ON CONFLICT REPLACE);",
        "CREATE INDEX IF NOT EXISTS idx_doc_path_model ON document_embeddings(doc_path, model_id);",
        "CREATE VIRTUAL TABLE IF NOT EXISTS document_chunks_fts USING fts5(chunk_text, content='document_embeddings', content_rowid='id');"
    };

    if (sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to begin schema migration: %s", sqlite3_errmsg(m_db));
        return false;
    }

    const int schema_version = getSchemaVersion();
    if (!runMigrations(schema_version) || !executeStatements(statements) || !setSchemaVersion(kCurrentSchemaVersion)) {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    std::vector<const char*> fts_statements = {
        "CREATE TRIGGER IF NOT EXISTS notes_ai AFTER INSERT ON notes BEGIN INSERT INTO notes_fts(rowid, title, content) VALUES (new.id, new.title, new.content); END;",
        "CREATE TRIGGER IF NOT EXISTS notes_ad AFTER DELETE ON notes BEGIN INSERT INTO notes_fts(notes_fts, rowid, title, content) VALUES ('delete', old.id, old.title, old.content); END;",
        "CREATE TRIGGER IF NOT EXISTS notes_au AFTER UPDATE ON notes BEGIN INSERT INTO notes_fts(notes_fts, rowid, title, content) VALUES ('delete', old.id, old.title, old.content); INSERT INTO notes_fts(rowid, title, content) VALUES (new.id, new.title, new.content); END;",
        "CREATE TRIGGER IF NOT EXISTS episodes_ai AFTER INSERT ON episodes BEGIN INSERT INTO episodes_fts(rowid, summary) VALUES (new.id, new.summary); END;",
        "CREATE TRIGGER IF NOT EXISTS episodes_ad AFTER DELETE ON episodes BEGIN INSERT INTO episodes_fts(episodes_fts, rowid, summary) VALUES ('delete', old.id, old.summary); END;",
        "CREATE TRIGGER IF NOT EXISTS episodes_au AFTER UPDATE ON episodes BEGIN INSERT INTO episodes_fts(episodes_fts, rowid, summary) VALUES ('delete', old.id, old.summary); INSERT INTO episodes_fts(rowid, summary) VALUES (new.id, new.summary); END;",
        "DROP TRIGGER IF EXISTS doc_chunks_ai;",
        "DROP TRIGGER IF EXISTS doc_chunks_ad;",
        "DROP TRIGGER IF EXISTS doc_chunks_au;",
        "CREATE TRIGGER IF NOT EXISTS document_embeddings_ai AFTER INSERT ON document_embeddings BEGIN INSERT INTO document_chunks_fts(rowid, chunk_text) VALUES (new.id, new.chunk_text); END;",
        "CREATE TRIGGER IF NOT EXISTS document_embeddings_ad AFTER DELETE ON document_embeddings BEGIN INSERT INTO document_chunks_fts(document_chunks_fts, rowid, chunk_text) VALUES ('delete', old.id, old.chunk_text); END;",
        "CREATE TRIGGER IF NOT EXISTS document_embeddings_au AFTER UPDATE ON document_embeddings BEGIN INSERT INTO document_chunks_fts(document_chunks_fts, rowid, chunk_text) VALUES ('delete', old.id, old.chunk_text); INSERT INTO document_chunks_fts(rowid, chunk_text) VALUES (new.id, new.chunk_text); END;",
        "INSERT INTO notes_fts(notes_fts) VALUES('rebuild');",
        "INSERT INTO episodes_fts(episodes_fts) VALUES('rebuild');",
        "INSERT INTO document_chunks_fts(document_chunks_fts) VALUES('rebuild');"
    };

    if (!executeStatements(fts_statements)) {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to commit schema migration: %s", sqlite3_errmsg(m_db));
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    return true;
}

int LongTermMemory::getSchemaVersion() {
    sqlite3_stmt* stmt = nullptr;
    int version = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT version FROM schema_version LIMIT 1;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            version = sqlite3_column_int(stmt, 0);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return version;
}

bool LongTermMemory::setSchemaVersion(int version) {
    if (sqlite3_exec(m_db, "DELETE FROM schema_version;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to clear schema_version: %s", sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT INTO schema_version(version) VALUES (?);", -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to prepare schema_version insert: %s", sqlite3_errmsg(m_db));
        return false;
    }
    sqlite3_bind_int(stmt, 1, version);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        LOGE(TAG, "Failed to set schema_version: %s", sqlite3_errmsg(m_db));
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool LongTermMemory::runMigrations(int current_version) {
    if (current_version < 1 && !migrateLegacyFactsTable()) {
        return false;
    }
    if (current_version < 3) {
        if (!tableHasColumn(m_db, "document_embeddings", "doc_hash")) {
            sqlite3_exec(m_db, "ALTER TABLE document_embeddings ADD COLUMN doc_hash TEXT NOT NULL DEFAULT '';", nullptr, nullptr, nullptr);
        }
        if (!tableHasColumn(m_db, "document_embeddings", "model_id")) {
            sqlite3_exec(m_db, "ALTER TABLE document_embeddings ADD COLUMN model_id TEXT NOT NULL DEFAULT 'embeddinggemma-2-270m';", nullptr, nullptr, nullptr);
        }
        sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_doc_path_model ON document_embeddings(doc_path, model_id);", nullptr, nullptr, nullptr);
    }
    return true;
}

bool LongTermMemory::migrateLegacyFactsTable() {
    if (!tableHasColumn(m_db, "facts", "key")) {
        return true;
    }

    const char* sql =
        "ALTER TABLE facts RENAME TO facts_legacy_v0;";
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to archive legacy facts table: %s", sqlite3_errmsg(m_db));
        return false;
    }
    LOGI(TAG, "Schema migration: archived legacy facts table as facts_legacy_v0.");
    return true;
}

bool LongTermMemory::executeStatements(const std::vector<const char*>& statements) {
    for (const char* sql : statements) {
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            LOGE(TAG, "Schema error on [%s]: %s", sql, sqlite3_errmsg(m_db));
            return false;
        }
    }
    return true;
}

bool LongTermMemory::storeNote(const std::string& title, const std::string& content, const std::string& tags) {
    if (!m_db || m_read_only.load()) return false;
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
    if (!m_db || m_read_only.load()) { LOGE(TAG, "storeFact failed: DB is null or ReadOnly."); return false; }
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
    if (m_read_only.load()) { LOGW(TAG, "storeVault blocked: Read-only mode active"); return false; }
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
    if (!m_db || m_read_only.load()) return false;
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
    if (!m_db || m_read_only.load()) return false;
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
    // v12.26: Secure matching. Only match if exact or substring of parameter, not the other way around if entity is too short.
    const char* sql = "SELECT value FROM facts WHERE "
                      "entity LIKE ? AND attribute LIKE ? "
                      "ORDER BY confidence DESC, id DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    std::string entity_param = "%" + entity + "%";
    std::string attr_param = "%" + attr + "%";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, entity_param.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, attr_param.c_str(), -1, SQLITE_TRANSIENT);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            if (val) return reinterpret_cast<const char*>(val);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return "";
}

std::vector<std::string> LongTermMemory::searchNotes(const std::string& query) {
    if (!m_db) return {};
    std::string safe_query = sanitizeFts5Query(query);
    if (safe_query.empty()) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    const char* sql = "SELECT title, content FROM notes_fts WHERE notes_fts MATCH ? ORDER BY rank LIMIT 5;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, safe_query.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string title = columnText(stmt, 0);
            std::string content = columnText(stmt, 1);
            if (content.length() > 250 || content.find("Analysis of") != std::string::npos || content.find("论") != std::string::npos || content.find("None") != std::string::npos || content.find("Executing plan") != std::string::npos) {
                continue;
            }
            results.push_back(title + ": " + content);
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::string> LongTermMemory::searchEpisodes(const std::string& query) {
    if (!m_db) return {};
    std::string safe_query = sanitizeFts5Query(query);
    if (safe_query.empty()) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    const char* sql = "SELECT summary FROM episodes_fts WHERE episodes_fts MATCH ? ORDER BY rank LIMIT 5;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, safe_query.c_str(), -1, SQLITE_TRANSIENT);
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
            results.push_back(columnText(stmt, 0));
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
                columnText(stmt, 0),
                columnText(stmt, 1)
            });
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

bool LongTermMemory::storeMessage(const std::string& role, const std::string& content, int64_t timestamp) {
    if (!m_db || m_read_only.load()) return false;
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
                columnText(stmt, 0),
                columnText(stmt, 1)
            });
        }
    }
    sqlite3_finalize(stmt);
    return history;
}

bool LongTermMemory::clearHistory() {
    if (!m_db || m_read_only.load()) return false;
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
    if (!m_db || m_read_only.load()) return false;
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
    if (!m_db || query.empty()) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    std::unordered_set<std::string> seen;

    std::vector<std::string> tokens;
    std::string current_token;
    for (char c : query) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') {
            current_token += c;
        } else {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
        }
    }
    if (!current_token.empty()) tokens.push_back(current_token);
    if (tokens.empty()) tokens.push_back(query);

    std::string sql = "SELECT DISTINCT path FROM file_index WHERE ";
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) sql += " AND ";
        sql += "(name LIKE ? OR path LIKE ?)";
    }
    sql += " LIMIT 20;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        std::vector<std::string> like_params;
        for (size_t i = 0; i < tokens.size(); ++i) {
            like_params.push_back("%" + tokens[i] + "%");
        }
        for (size_t i = 0; i < tokens.size(); ++i) {
            sqlite3_bind_text(stmt, static_cast<int>(2 * i + 1), like_params[i].c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, static_cast<int>(2 * i + 2), like_params[i].c_str(), -1, SQLITE_TRANSIENT);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string path = columnText(stmt, 0);
            if (seen.insert(path).second) {  // deduplicate
                results.push_back(path);
            }
        }
    }
    sqlite3_finalize(stmt);
    return results;
}


std::vector<std::string> LongTermMemory::search(const std::string& query) { return searchNotes(query); }
bool LongTermMemory::consolidate(const std::string& summary) { return storeNote("Consolidated Summary", summary, "auto"); }
void LongTermMemory::applyDecay(uint64_t current_timestamp) {
    if (!m_db || m_read_only.load()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (current_timestamp == 0) {
        current_timestamp = std::time(nullptr);
    }
    const double lambda_inferred = 1.0e-6; // Standard Ebbinghaus decay coefficient (~11 days time constant)
    const double lambda_explicit = 1.0e-7; // Slower decay for explicit user facts (~115 days time constant)
    
    // 1. Prune inferred/OCR/belief facts whose retention score falls below critical threshold 0.15
    // Retention Score = confidence * exp(-lambda * dt)
    sqlite3_stmt* stmt = nullptr;
    int pruned_facts = 0;
    const char* select_sql = "SELECT id, source_type, confidence, last_verified_at FROM facts;";
    std::vector<int> to_delete;
    std::vector<std::pair<int, double>> to_update;
    
    if (sqlite3_prepare_v2(m_db, select_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            int source_type = sqlite3_column_int(stmt, 1);
            double conf = sqlite3_column_double(stmt, 2);
            uint64_t last_verified = sqlite3_column_int64(stmt, 3);
            
            uint64_t dt = (current_timestamp > last_verified) ? (current_timestamp - last_verified) : 0;
            double lambda = (source_type == 0) ? lambda_explicit : lambda_inferred;
            double retention = conf * std::exp(-lambda * static_cast<double>(dt));
            
            if (retention < 0.15 && source_type != 0) {
                to_delete.push_back(id);
            } else if (std::abs(retention - conf) > 0.01) {
                to_update.push_back({id, retention});
            }
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    
    for (int id : to_delete) {
        std::string del_sql = "DELETE FROM facts WHERE id = " + std::to_string(id) + ";";
        sqlite3_exec(m_db, del_sql.c_str(), nullptr, nullptr, nullptr);
        pruned_facts++;
    }
    for (const auto& [id, new_conf] : to_update) {
        std::string upd_sql = "UPDATE facts SET confidence = " + std::to_string(new_conf) + " WHERE id = " + std::to_string(id) + ";";
        sqlite3_exec(m_db, upd_sql.c_str(), nullptr, nullptr, nullptr);
    }
    
    // 2. Prune old failed/unreinforced episodes older than 30 days (2592000 sec)
    uint64_t thirty_days_ago = (current_timestamp > 2592000) ? (current_timestamp - 2592000) : 0;
    std::string ep_del_sql = "DELETE FROM episodes WHERE timestamp < " + std::to_string(thirty_days_ago) + " AND outcome_enum = 0;";
    sqlite3_exec(m_db, ep_del_sql.c_str(), nullptr, nullptr, nullptr);
    
    LOGI(TAG, "Applied Ebbinghaus memory decay: pruned %d expired records, updated confidence scores.", pruned_facts);
}
int LongTermMemory::runMaintenance(bool) { return 0; }
bool LongTermMemory::storeAuditLog(const std::string& action, const std::string& details) {
    if (!m_db) { LOGE(TAG, "storeAuditLog failed: DB is null."); return false; }
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO audit (action, details, timestamp) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, action.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, details.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, std::time(nullptr));
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!success) { LOGE(TAG, "storeAuditLog step failed: %s", sqlite3_errmsg(m_db)); }
        sqlite3_finalize(stmt);
        return success;
    } else {
        LOGE(TAG, "storeAuditLog prepare failed: %s", sqlite3_errmsg(m_db));
    }
    return false;
}

std::vector<LongTermMemory::EpisodeRecord> LongTermMemory::getRecentEpisodes(int limit) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<EpisodeRecord> results;
    const char* sql = "SELECT intent, summary, payload_json, outcome_enum, timestamp FROM episodes ORDER BY timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            EpisodeRecord rec;
            rec.intent = columnText(stmt, 0);
            rec.summary = columnText(stmt, 1);
            rec.payload_json = columnText(stmt, 2);
            rec.success = (sqlite3_column_int(stmt, 3) != 0);
            rec.timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
            results.push_back(rec);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return results;
}

std::vector<LongTermMemory::EpisodeRecord> LongTermMemory::getRecentFailures(int limit) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<EpisodeRecord> results;
    const char* sql = "SELECT intent, summary, payload_json, outcome_enum, timestamp FROM episodes WHERE outcome_enum = 0 ORDER BY timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            EpisodeRecord rec;
            rec.intent = columnText(stmt, 0);
            rec.summary = columnText(stmt, 1);
            rec.payload_json = columnText(stmt, 2);
            rec.success = false;
            rec.timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
            results.push_back(rec);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return results;
}

bool LongTermMemory::storeFailure(const std::string& node_id, int failure_type, int retry_count, const std::string& resolution) {
    if (!m_db || m_read_only.load()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO failures (node_id, failure_type, timestamp, retry_count, resolution) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, node_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, failure_type);
    sqlite3_bind_int64(stmt, 3, std::time(nullptr) * 1000); // ms
    sqlite3_bind_int(stmt, 4, retry_count);
    sqlite3_bind_text(stmt, 5, resolution.c_str(), -1, SQLITE_TRANSIENT);
    
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<Ronin::Kernel::FailureRecord> LongTermMemory::getFailures(int limit) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Ronin::Kernel::FailureRecord> results;
    const char* sql = "SELECT node_id, failure_type, timestamp, retry_count, resolution FROM failures ORDER BY timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Ronin::Kernel::FailureRecord rec;
            rec.node_id = columnText(stmt, 0);
            rec.type = static_cast<FailureType>(sqlite3_column_int(stmt, 1));
            rec.timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
            rec.retry_count = sqlite3_column_int(stmt, 3);
            rec.resolution = columnText(stmt, 4);
            results.push_back(rec);
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<Ronin::Kernel::FailureRecord> LongTermMemory::getFailuresByNode(const std::string& node_id, int limit) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Ronin::Kernel::FailureRecord> results;
    const char* sql = "SELECT node_id, failure_type, timestamp, retry_count, resolution FROM failures WHERE node_id = ? ORDER BY timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, node_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Ronin::Kernel::FailureRecord rec;
            rec.node_id = columnText(stmt, 0);
            rec.type = static_cast<FailureType>(sqlite3_column_int(stmt, 1));
            rec.timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
            rec.retry_count = sqlite3_column_int(stmt, 3);
            rec.resolution = columnText(stmt, 4);
            results.push_back(rec);
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

int LongTermMemory::countFailures(const std::string& node_id, uint64_t since_ms) {
    if (!m_db) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT COUNT(*) FROM failures WHERE node_id = ? AND timestamp > ?;";
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, node_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(since_ms));
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

std::string LongTermMemory::getLatestPerceptionState() {
    if (!m_db) return "unknown";
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT state_value FROM perception_history ORDER BY timestamp DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    std::string val = "unknown";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) val = reinterpret_cast<const char*>(text);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return val;
}

void LongTermMemory::l2Normalize(std::vector<float>& vec) {
    if (vec.empty()) return;
    double sum_sq = 0.0;
    for (float v : vec) {
        sum_sq += static_cast<double>(v) * static_cast<double>(v);
    }
    if (sum_sq > 1e-12) {
        float inv_norm = static_cast<float>(1.0 / std::sqrt(sum_sq));
        for (float& v : vec) {
            v *= inv_norm;
        }
    }
}

float LongTermMemory::alignedDotProduct(const float* a, const float* b, size_t dim) {
    if (!a || !b || dim == 0) return 0.0f;
    // Unrolled 4-way loop friendly to ARM NEON / auto-vectorization
    float sum0 = 0.0f, sum1 = 0.0f, sum2 = 0.0f, sum3 = 0.0f;
    size_t i = 0;
    for (; i + 3 < dim; i += 4) {
        sum0 += a[i] * b[i];
        sum1 += a[i + 1] * b[i + 1];
        sum2 += a[i + 2] * b[i + 2];
        sum3 += a[i + 3] * b[i + 3];
    }
    float total = (sum0 + sum1) + (sum2 + sum3);
    for (; i < dim; ++i) {
        total += a[i] * b[i];
    }
    return total;
}

float LongTermMemory::cosineSimilarity(const float* a, const float* b, size_t dim) {
    if (!a || !b || dim == 0) return 0.0f;
    double dot = 0.0, norm_a = 0.0, norm_b = 0.0;
    for (size_t i = 0; i < dim; ++i) {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        norm_a += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        norm_b += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }
    double denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    if (denom <= 1e-12) return 0.0f;
    return static_cast<float>(dot / denom);
}

bool LongTermMemory::isDocumentIndexed(const std::string& doc_path, const std::string& doc_hash, const std::string& model_id) {
    if (!m_db || doc_path.empty() || doc_hash.empty()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "SELECT id FROM document_embeddings WHERE doc_path = ? AND doc_hash = ? AND model_id = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, doc_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, doc_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, model_id.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

bool LongTermMemory::deleteDocumentEmbeddings(const std::string& doc_path, const std::string& model_id) {
    if (!m_db || doc_path.empty()) return false;
    if (m_read_only.load()) {
        LOGW(TAG, "deleteDocumentEmbeddings blocked: Read-only SafeMode active");
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = model_id.empty()
        ? "DELETE FROM document_embeddings WHERE doc_path = ?;"
        : "DELETE FROM document_embeddings WHERE doc_path = ? AND model_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, doc_path.c_str(), -1, SQLITE_TRANSIENT);
    if (!model_id.empty()) {
        sqlite3_bind_text(stmt, 2, model_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool LongTermMemory::storeDocumentEmbedding(const std::string& doc_path, const std::string& doc_hash, int chunk_index, const std::string& chunk_text, const std::vector<float>& embedding, const std::string& model_id) {
    if (!m_db || embedding.empty()) return false;
    if (m_read_only.load()) {
        LOGW(TAG, "storeDocumentEmbedding blocked: Read-only SafeMode active");
        return false;
    }

    std::vector<float> normalized = embedding;
    l2Normalize(normalized);

    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO document_embeddings (doc_path, doc_hash, chunk_index, chunk_text, embedding_blob, model_id, dimension, created_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGE(TAG, "storeDocumentEmbedding prepare failed: %s", sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, doc_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, doc_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, chunk_index);
    sqlite3_bind_text(stmt, 4, chunk_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 5, normalized.data(), static_cast<int>(normalized.size() * sizeof(float)), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, model_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, static_cast<int>(normalized.size()));
    sqlite3_bind_int64(stmt, 8, std::time(nullptr));

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok) {
        LOGE(TAG, "storeDocumentEmbedding step failed: %s", sqlite3_errmsg(m_db));
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool LongTermMemory::storeDocumentEmbedding(const std::string& doc_path, int chunk_index, const std::string& chunk_text, const std::vector<float>& embedding) {
    return storeDocumentEmbedding(doc_path, "", chunk_index, chunk_text, embedding, "embeddinggemma-2-270m");
}

bool LongTermMemory::storeDocumentChunksAtomic(
    const std::string& doc_path,
    const std::string& doc_hash,
    const std::string& model_id,
    const std::vector<DocumentChunk>& chunks) {
    if (!m_db || doc_path.empty() || chunks.empty()) return false;
    if (m_read_only.load()) {
        LOGW(TAG, "storeDocumentChunksAtomic blocked: Read-only SafeMode active");
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "storeDocumentChunksAtomic begin transaction failed: %s", sqlite3_errmsg(m_db));
        return false;
    }

    // 1. Delete all existing chunks for this (doc_path, model_id) so stale trailing chunks never remain
    const char* del_sql = "DELETE FROM document_embeddings WHERE doc_path = ? AND model_id = ?;";
    sqlite3_stmt* del_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, del_sql, -1, &del_stmt, nullptr) != SQLITE_OK) {
        LOGE(TAG, "storeDocumentChunksAtomic prepare delete failed: %s", sqlite3_errmsg(m_db));
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_text(del_stmt, 1, doc_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(del_stmt, 2, model_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(del_stmt) != SQLITE_DONE) {
        LOGE(TAG, "storeDocumentChunksAtomic delete failed: %s", sqlite3_errmsg(m_db));
        sqlite3_finalize(del_stmt);
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_finalize(del_stmt);

    // 2. Insert new chunks
    const char* ins_sql = "INSERT INTO document_embeddings (doc_path, doc_hash, chunk_index, chunk_text, embedding_blob, model_id, dimension, created_at) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* ins_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, ins_sql, -1, &ins_stmt, nullptr) != SQLITE_OK) {
        LOGE(TAG, "storeDocumentChunksAtomic prepare insert failed: %s", sqlite3_errmsg(m_db));
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    int64_t now = std::time(nullptr);
    for (const auto& chunk : chunks) {
        if (chunk.embedding.empty()) continue;
        std::vector<float> normalized = chunk.embedding;
        l2Normalize(normalized);

        sqlite3_reset(ins_stmt);
        sqlite3_clear_bindings(ins_stmt);
        sqlite3_bind_text(ins_stmt, 1, doc_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 2, doc_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins_stmt, 3, chunk.index);
        sqlite3_bind_text(ins_stmt, 4, chunk.text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(ins_stmt, 5, normalized.data(), static_cast<int>(normalized.size() * sizeof(float)), SQLITE_TRANSIENT);
        sqlite3_bind_text(ins_stmt, 6, model_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins_stmt, 7, static_cast<int>(normalized.size()));
        sqlite3_bind_int64(ins_stmt, 8, now);

        if (sqlite3_step(ins_stmt) != SQLITE_DONE) {
            LOGE(TAG, "storeDocumentChunksAtomic insert chunk %d failed: %s", chunk.index, sqlite3_errmsg(m_db));
            sqlite3_finalize(ins_stmt);
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }
    sqlite3_finalize(ins_stmt);

    if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "storeDocumentChunksAtomic commit failed: %s", sqlite3_errmsg(m_db));
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    return true;
}

std::vector<LongTermMemory::HybridSearchMatch> LongTermMemory::searchEmbeddings(
    const std::vector<float>& query_embedding, const std::string& doc_path_filter, int top_k, const std::string& model_id) {
    if (!m_db || query_embedding.empty() || top_k <= 0) return {};

    std::vector<float> q_norm = query_embedding;
    l2Normalize(q_norm);
    const size_t q_dim = q_norm.size();

    // 16-byte aligned query buffer for SIMD dot product
    alignas(16) float q_aligned[1024];
    const float* q_ptr = q_norm.data();
    if (q_dim <= 1024) {
        std::memcpy(q_aligned, q_norm.data(), q_dim * sizeof(float));
        q_ptr = q_aligned;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::string sql = "SELECT doc_path, chunk_index, chunk_text, embedding_blob, dimension, model_id FROM document_embeddings WHERE 1=1";
    if (!model_id.empty()) {
        sql += " AND model_id = ?";
    }
    if (!doc_path_filter.empty()) {
        sql += " AND doc_path = ?";
    }
    sql += ";";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return {};
    int param_idx = 1;
    if (!model_id.empty()) {
        sqlite3_bind_text(stmt, param_idx++, model_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!doc_path_filter.empty()) {
        sqlite3_bind_text(stmt, param_idx++, doc_path_filter.c_str(), -1, SQLITE_TRANSIENT);
    }

    std::vector<HybridSearchMatch> candidates;
    alignas(16) float row_aligned[1024];
    std::vector<float> fallback_buf;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int dim = sqlite3_column_int(stmt, 4);
        int blob_bytes = sqlite3_column_bytes(stmt, 3);
        const void* blob_ptr = sqlite3_column_blob(stmt, 3);
        if (!blob_ptr || dim <= 0 || static_cast<size_t>(dim) != q_dim || blob_bytes != static_cast<int>(dim * sizeof(float))) {
            continue;
        }

        const float* row_ptr = nullptr;
        if (q_dim <= 1024) {
            std::memcpy(row_aligned, blob_ptr, q_dim * sizeof(float));
            row_ptr = row_aligned;
        } else {
            fallback_buf.resize(q_dim);
            std::memcpy(fallback_buf.data(), blob_ptr, q_dim * sizeof(float));
            row_ptr = fallback_buf.data();
        }

        float sim = alignedDotProduct(q_ptr, row_ptr, q_dim);
        HybridSearchMatch match;
        match.doc_path = columnText(stmt, 0);
        match.chunk_index = sqlite3_column_int(stmt, 1);
        match.chunk_text = columnText(stmt, 2);
        match.cosine_score = sim;
        match.rrf_score = sim;
        match.model_id = columnText(stmt, 5);
        candidates.push_back(std::move(match));
    }
    sqlite3_finalize(stmt);

    std::sort(candidates.begin(), candidates.end(), [](const HybridSearchMatch& a, const HybridSearchMatch& b) {
        return a.cosine_score > b.cosine_score;
    });

    if (static_cast<int>(candidates.size()) > top_k) {
        candidates.resize(static_cast<size_t>(top_k));
    }
    return candidates;
}

std::vector<LongTermMemory::HybridSearchMatch> LongTermMemory::hybridSearch(
    const std::string& query_text, const std::vector<float>& query_embedding, const std::string& doc_path_filter, int top_k, const std::string& model_id) {
    if (!m_db || top_k <= 0) return {};

    // 1. Tokenize query text for lexical and FTS5 BM25 search
    std::vector<std::string> query_tokens;
    if (!query_text.empty()) {
        std::string lower_q = query_text;
        std::transform(lower_q.begin(), lower_q.end(), lower_q.begin(), [](unsigned char c) { return std::tolower(c); });
        std::stringstream ss(lower_q);
        std::string tok;
        while (ss >> tok) {
            if (tok.size() >= 2) query_tokens.push_back(tok);
        }
        auto seg_tokens = segmentText(query_text);
        for (const auto& st : seg_tokens) {
            if (st.size() >= 2 && std::find(query_tokens.begin(), query_tokens.end(), st) == query_tokens.end()) {
                query_tokens.push_back(st);
            }
        }
    }

    // 2. Prepare normalized query embedding
    std::vector<float> q_norm = query_embedding;
    if (!q_norm.empty()) l2Normalize(q_norm);
    const size_t q_dim = q_norm.size();

    alignas(16) float q_aligned[1024];
    const float* q_ptr = q_norm.data();
    if (!q_norm.empty() && q_dim <= 1024) {
        std::memcpy(q_aligned, q_norm.data(), q_dim * sizeof(float));
        q_ptr = q_aligned;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // 3. Optional FTS5 BM25 lookup
    std::unordered_map<int64_t, int> fts_ranks; // rowid -> 1-based rank
    std::unordered_map<int64_t, float> fts_bm25_scores;
    if (!query_tokens.empty()) {
        std::string fts_query;
        for (const auto& qt : query_tokens) {
            std::string clean = sanitizeFts5Query(qt);
            if (clean.empty()) continue;
            // Trim whitespace
            size_t s = clean.find_first_not_of(" \t\r\n");
            if (s == std::string::npos) continue;
            size_t e = clean.find_last_not_of(" \t\r\n");
            std::string word = clean.substr(s, e - s + 1);
            if (word.empty()) continue;
            if (!fts_query.empty()) fts_query += " OR ";
            fts_query += "\"" + word + "\"";
        }

        if (!fts_query.empty()) {
            const char* fts_sql = "SELECT rowid, bm25(document_chunks_fts) FROM document_chunks_fts WHERE document_chunks_fts MATCH ? ORDER BY bm25(document_chunks_fts) ASC LIMIT 100;";
            sqlite3_stmt* fts_stmt = nullptr;
            if (sqlite3_prepare_v2(m_db, fts_sql, -1, &fts_stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(fts_stmt, 1, fts_query.c_str(), -1, SQLITE_TRANSIENT);
                int rank = 1;
                while (sqlite3_step(fts_stmt) == SQLITE_ROW) {
                    int64_t rowid = sqlite3_column_int64(fts_stmt, 0);
                    double bm25 = sqlite3_column_double(fts_stmt, 1);
                    fts_ranks[rowid] = rank++;
                    fts_bm25_scores[rowid] = static_cast<float>(bm25);
                }
                sqlite3_finalize(fts_stmt);
            }
        }
    }

    // 4. Retrieve candidate chunks with optional model_id and doc_path filters
    std::string sql = "SELECT id, doc_path, chunk_index, chunk_text, embedding_blob, dimension, model_id FROM document_embeddings WHERE 1=1";
    if (!model_id.empty()) {
        sql += " AND model_id = ?";
    }
    if (!doc_path_filter.empty()) {
        sql += " AND doc_path = ?";
    }
    sql += ";";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return {};
    int param_idx = 1;
    if (!model_id.empty()) {
        sqlite3_bind_text(stmt, param_idx++, model_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!doc_path_filter.empty()) {
        sqlite3_bind_text(stmt, param_idx++, doc_path_filter.c_str(), -1, SQLITE_TRANSIENT);
    }

    struct Candidate {
        int64_t id = 0;
        HybridSearchMatch match;
        int vec_rank = 0;
        int fts_rank = 0;
    };

    std::vector<Candidate> candidates;
    alignas(16) float row_aligned[1024];
    std::vector<float> fallback_buf;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Candidate c;
        c.id = sqlite3_column_int64(stmt, 0);
        c.match.doc_path = columnText(stmt, 1);
        c.match.chunk_index = sqlite3_column_int(stmt, 2);
        c.match.chunk_text = columnText(stmt, 3);
        c.match.model_id = columnText(stmt, 6);

        // Vector similarity
        int dim = sqlite3_column_int(stmt, 5);
        int blob_bytes = sqlite3_column_bytes(stmt, 4);
        const void* blob_ptr = sqlite3_column_blob(stmt, 4);
        if (q_dim > 0 && blob_ptr && dim > 0 && static_cast<size_t>(dim) == q_dim && blob_bytes == static_cast<int>(dim * sizeof(float))) {
            const float* row_ptr = nullptr;
            if (q_dim <= 1024) {
                std::memcpy(row_aligned, blob_ptr, q_dim * sizeof(float));
                row_ptr = row_aligned;
            } else {
                fallback_buf.resize(q_dim);
                std::memcpy(fallback_buf.data(), blob_ptr, q_dim * sizeof(float));
                row_ptr = fallback_buf.data();
            }
            c.match.cosine_score = alignedDotProduct(q_ptr, row_ptr, q_dim);
        }

        // FTS5 rank lookup
        auto it_fts = fts_ranks.find(c.id);
        if (it_fts != fts_ranks.end()) {
            c.fts_rank = it_fts->second;
            c.match.fts_bm25_score = fts_bm25_scores[c.id];
        }

        // Lexical token hits count
        if (!query_tokens.empty()) {
            std::string lower_chunk = c.match.chunk_text;
            std::transform(lower_chunk.begin(), lower_chunk.end(), lower_chunk.begin(), [](unsigned char ch) { return std::tolower(ch); });
            int hits = 0;
            for (const auto& qt : query_tokens) {
                size_t pos = 0;
                while ((pos = lower_chunk.find(qt, pos)) != std::string::npos) {
                    hits++;
                    pos += qt.size();
                }
            }
            c.match.lexical_hits = hits;
        }

        candidates.push_back(std::move(c));
    }
    sqlite3_finalize(stmt);

    if (candidates.empty()) return {};

    // 5. Rank vector candidates
    if (q_dim > 0) {
        std::vector<size_t> vec_order(candidates.size());
        for (size_t i = 0; i < candidates.size(); ++i) vec_order[i] = i;
        std::sort(vec_order.begin(), vec_order.end(), [&](size_t a, size_t b) {
            return candidates[a].match.cosine_score > candidates[b].match.cosine_score;
        });
        for (size_t rank = 0; rank < vec_order.size(); ++rank) {
            candidates[vec_order[rank]].vec_rank = static_cast<int>(rank + 1);
        }
    }

    // 6. Reciprocal Rank Fusion: RRF = 1 / (60 + rank_fts) + 1 / (60 + rank_vec)
    constexpr float kRrfK = 60.0f;
    for (auto& c : candidates) {
        float rrf = 0.0f;
        if (c.fts_rank > 0) {
            rrf += 1.0f / (kRrfK + static_cast<float>(c.fts_rank));
        } else if (c.match.lexical_hits > 0) {
            // Lexical fallback ranking if FTS virtual table had no exact index match
            rrf += 0.5f / (kRrfK + 10.0f);
        }
        if (c.vec_rank > 0) {
            rrf += 1.0f / (kRrfK + static_cast<float>(c.vec_rank));
        }
        c.match.rrf_score = rrf;
    }

    // 7. Sort by RRF score descending, tie-breaking by cosine score and lexical hits
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (std::abs(a.match.rrf_score - b.match.rrf_score) > 1e-6f) {
            return a.match.rrf_score > b.match.rrf_score;
        }
        if (std::abs(a.match.cosine_score - b.match.cosine_score) > 1e-6f) {
            return a.match.cosine_score > b.match.cosine_score;
        }
        return a.match.lexical_hits > b.match.lexical_hits;
    });

    std::vector<HybridSearchMatch> results;
    results.reserve(std::min(top_k, static_cast<int>(candidates.size())));
    for (auto& c : candidates) {
        if (c.match.rrf_score <= 0.0f) continue;
        results.push_back(std::move(c.match));
        if (static_cast<int>(results.size()) >= top_k) break;
    }
    return results;
}

} // namespace Ronin::Kernel::Memory
