#include "long_term_memory.h"
#include <cmath>
#include <ctime>
#include <vector>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cstdint>
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
    const char* schema = 
        "CREATE TABLE IF NOT EXISTS memories ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  original_text_mm TEXT, "
        "  segmented_text_mm TEXT, "
        "  tier_enum INTEGER DEFAULT 0, " // 0:NOTE, 1:FACT, 2:VAULT
        "  entity_attr TEXT, "            // For FACT mapping
        "  importance_score REAL DEFAULT 1.0, "
        "  recall_count INTEGER DEFAULT 0, "
        "  last_accessed_time INTEGER, "
        "  creation_time INTEGER, "
        "  state_enum INTEGER DEFAULT 0);" 
        
        "CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5("
        "  original_text_mm, "
        "  content_id UNINDEXED"
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
        "  last_modified UNINDEXED"
        ");"
        
        "CREATE TABLE IF NOT EXISTS audit ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  action TEXT, "
        "  details TEXT, "
        "  timestamp INTEGER);";

    if (sqlite3_exec(m_db, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to create SQLite schema: %s", sqlite3_errmsg(m_db));
        return false;
    }

    // Migration: Add columns for v10.0 if upgrading from legacy build
    sqlite3_exec(m_db, "ALTER TABLE memories ADD COLUMN tier_enum INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "ALTER TABLE memories ADD COLUMN entity_attr TEXT;", nullptr, nullptr, nullptr);

    return true;
}

bool LongTermMemory::storeNote(const std::string& content) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::string segmented = content;
    if (m_segmenter) segmented = m_segmenter->segment(content);

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* sql = "INSERT INTO memories (original_text_mm, segmented_text_mm, tier_enum, creation_time, last_accessed_time) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    uint64_t now = std::time(nullptr);

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, content.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, segmented.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, static_cast<int>(MemoryTier::NOTE));
        sqlite3_bind_int64(stmt, 4, now);
        sqlite3_bind_int64(stmt, 5, now);
        sqlite3_step(stmt);
        sqlite3_int64 row_id = sqlite3_last_insert_rowid(m_db);
        sqlite3_finalize(stmt);

        const char* fts_sql = "INSERT INTO memories_fts (original_text_mm, content_id) VALUES (?, ?);";
        sqlite3_stmt* fts_stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, fts_sql, -1, &fts_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(fts_stmt, 1, segmented.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(fts_stmt, 2, row_id);
            sqlite3_step(fts_stmt);
            sqlite3_finalize(fts_stmt);
        }
    }
    
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool LongTermMemory::storeFact(const std::string& entity, const std::string& attr, const std::string& value) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t now = std::time(nullptr);
    std::string entity_attr = entity + ":" + attr;

    const char* sql = "INSERT INTO memories (original_text_mm, entity_attr, tier_enum, creation_time, last_accessed_time) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, entity_attr.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, static_cast<int>(MemoryTier::FACT));
        sqlite3_bind_int64(stmt, 4, now);
        sqlite3_bind_int64(stmt, 5, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return true;
    }
    return false;
}

bool LongTermMemory::storeVault(const std::string& key, const std::string& encrypted_value) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t now = std::time(nullptr);

    const char* sql = "INSERT INTO memories (original_text_mm, entity_attr, tier_enum, creation_time, last_accessed_time) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, encrypted_value.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, static_cast<int>(MemoryTier::VAULT));
        sqlite3_bind_int64(stmt, 4, now);
        sqlite3_bind_int64(stmt, 5, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return true;
    }
    return false;
}

std::string LongTermMemory::retrieveFact(const std::string& entity, const std::string& attr) {
    if (!m_db) return "";
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string entity_attr = entity + ":" + attr;
    const char* sql = "SELECT original_text_mm FROM memories WHERE entity_attr = ? AND tier_enum = 1 ORDER BY last_accessed_time DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    std::string result = "";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, entity_attr.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            if (val) result = reinterpret_cast<const char*>(val);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> LongTermMemory::search(const std::string& query) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> results;
    
    // Segment the query to match the tokenized index
    std::string processed_query = query;
    if (m_segmenter) {
        processed_query = m_segmenter->segment(query);
    }
    
    // If segmentation resulted in empty (all stop words), fallback to raw query
    if (processed_query.empty()) processed_query = query;

    const char* sql = "SELECT original_text_mm FROM memories_fts WHERE memories_fts MATCH ? ORDER BY rank LIMIT 5;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, processed_query.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* content_ptr = sqlite3_column_text(stmt, 0);
            if (content_ptr) results.push_back(reinterpret_cast<const char*>(content_ptr));
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

bool LongTermMemory::consolidate(const std::string& summary_text) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Segment the text before storing for FTS5
    std::string segmented = summary_text;
    if (m_segmenter) {
        segmented = m_segmenter->segment(summary_text);
    }

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* sql = "INSERT INTO memories (original_text_mm, creation_time, last_accessed_time) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    uint64_t now = std::time(nullptr);

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_text(stmt, 1, summary_text.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, now);
    sqlite3_bind_int64(stmt, 3, now);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    sqlite3_int64 last_id = sqlite3_last_insert_rowid(m_db);
    sqlite3_finalize(stmt);

    const char* fts_sql = "INSERT INTO memories_fts (original_text_mm, content_id) VALUES (?, ?);";
    sqlite3_stmt* fts_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, fts_sql, -1, &fts_stmt, nullptr) == SQLITE_OK) {
        // Use segmented text for FTS5 indexing
        sqlite3_bind_text(fts_stmt, 1, segmented.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(fts_stmt, 2, last_id);
        sqlite3_step(fts_stmt);
        sqlite3_finalize(fts_stmt);
    }

    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool LongTermMemory::loadSegmenter(const std::string& dict_path) {
    if (m_segmenter) {
        return m_segmenter->loadDictionary(dict_path);
    }
    return false;
}

bool LongTermMemory::indexFile(const std::string& name, const std::string& path, const std::string& ext, uint64_t modified) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT OR REPLACE INTO file_index (name, path, extension, last_modified) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, ext.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(modified));

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
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
            const unsigned char* path = sqlite3_column_text(stmt, 0);
            if (path) results.push_back(reinterpret_cast<const char*>(path));
        }
    }
    sqlite3_finalize(stmt);
    return results;
}

void LongTermMemory::applyDecay(uint64_t current_timestamp) {
    // Legacy stability logic simplified or removed
}

// Thinking process များကို ဖယ်ထုတ်ပေးမည့် Lightweight Function
static std::string filterThinking(const std::string& input) {
    std::string output = input;
    size_t start_pos, end_pos;

    // [THINK] နှင့် [/THINK] ကြားရှိစာသားများကို loop ပတ်၍ ဖြတ်ထုတ်ခြင်း
    while ((start_pos = output.find("[THINK]")) != std::string::npos) {
        end_pos = output.find("[/THINK]");
        if (end_pos != std::string::npos && end_pos > start_pos) {
            // [/THINK] အပိတ် tag ပါအပါအဝင် အကုန်ဖြတ်မည် (length = end_pos - start_pos + 8)
            output.erase(start_pos, (end_pos - start_pos) + 8);
        } else {
            // အပိတ် tag မပါသေးပါက [THINK] ကနေ အဆုံးထိ ဖြတ်ပစ်မည်
            output.erase(start_pos);
            break;
        }
    }
    return output;
}

bool LongTermMemory::storeMessage(const std::string& role, const std::string& content, int64_t timestamp) {
    if (!m_db) return false;
    
    std::string final_content = content;
    // Assistant ဆီကလာတဲ့ output ဆိုရင် DB ထဲမသွင်းခင် တွေးချက်တွေကို ဖြတ်ထုတ်မည်
    if (role == "assistant") {
        final_content = filterThinking(content);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO chat_history (role, content, timestamp) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, role.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, final_content.c_str(), -1, SQLITE_STATIC);
    
    int64_t final_ts = (timestamp == 0) ? std::time(nullptr) : timestamp;
    sqlite3_bind_int64(stmt, 3, final_ts);
    
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

std::vector<std::pair<std::string, std::string>> LongTermMemory::getHistory(int limit, int offset) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::pair<std::string, std::string>> history;
    
    // Fetch the most recent messages first, then reverse them to restore chronological order
    const char* sql = "SELECT role, content FROM chat_history ORDER BY id DESC LIMIT ? OFFSET ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        sqlite3_bind_int(stmt, 2, offset);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* role = sqlite3_column_text(stmt, 0);
            const unsigned char* content = sqlite3_column_text(stmt, 1);
            if (role && content) {
                history.push_back({reinterpret_cast<const char*>(role), reinterpret_cast<const char*>(content)});
            }
        }
    }
    sqlite3_finalize(stmt);
    
    // Reverse to get chronological order (oldest to newest)
    std::reverse(history.begin(), history.end());
    return history;
}

bool LongTermMemory::clearHistory() {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "DELETE FROM chat_history;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool LongTermMemory::storeAuditLog(const std::string& action, const std::string& details) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO audit (action, details, timestamp) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, action.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, details.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, std::time(nullptr));
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

} // namespace Ronin::Kernel::Memory
