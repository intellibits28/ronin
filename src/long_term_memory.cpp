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

namespace {
// Phase 2.1: IEEE 754 Half-Precision (Float16) Helpers for Semantic Memory
uint16_t floatToHalf(float f) {
    uint32_t i = *(uint32_t*)&f;
    uint32_t s = (i >> 16) & 0x00008000;
    uint32_t e = ((i >> 23) & 0x000000ff) - (127 - 15);
    uint32_t m = i & 0x007fffff;
    if (e <= 0) {
        if (e < -10) return s;
        m = (m | 0x00800000) >> (1 - e);
        return s | (m >> 13);
    } else if (e == 0xff - (127 - 15)) {
        if (m == 0) return s | 0x7c00;
        return s | 0x7c00 | (m >> 13) | (m == 0 ? 0 : 1);
    } else {
        if (e > 30) return s | 0x7c00;
        return s | (e << 10) | (m >> 13);
    }
}

float halfToFloat(uint16_t h) {
    uint32_t s = (h & 0x8000) << 16;
    uint32_t e = (h & 0x7c00) >> 10;
    uint32_t m = (h & 0x03ff) << 13;
    if (e == 0) {
        if (m == 0) return *(float*)&s;
        while ((m & 0x00800000) == 0) { m <<= 1; e--; }
        e++; m &= ~0x00800000;
    } else if (e == 31) {
        uint32_t res = s | 0x7f800000 | m;
        return *(float*)&res;
    }
    e = e + (127 - 15);
    uint32_t res = s | (e << 23) | m;
    return *(float*)&res;
}
} // namespace

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
        
        "CREATE TABLE IF NOT EXISTS summaries ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  content TEXT, "
        "  embedding BLOB, "
        "  recall_count INTEGER DEFAULT 0, "
        "  last_accessed INTEGER, "
        "  state_enum INTEGER DEFAULT 0, " // 0=Active, 1=Cold, 2=Archived, 3=Forgotten, 4=Tombstoned
        "  timestamp INTEGER);"
        
        "CREATE VIRTUAL TABLE IF NOT EXISTS summaries_fts USING fts5("
        "  content, "
        "  content_id UNINDEXED"
        ");"
        
        "CREATE TABLE IF NOT EXISTS chat_history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  role TEXT, "
        "  content TEXT, "
        "  timestamp INTEGER);"
        
        "CREATE TABLE IF NOT EXISTS vectorized_interactions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  content TEXT, "
        "  embedding BLOB, "
        "  state_enum INTEGER DEFAULT 0, "
        "  timestamp INTEGER);"
        
        "CREATE VIRTUAL TABLE IF NOT EXISTS file_index USING fts5("
        "  name, "
        "  path, "
        "  extension, "
        "  last_modified UNINDEXED, "
        "  embedding_vector UNINDEXED"
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
    LOGI(TAG, "Memory Model v2.1: Executing State-Based Lifecycle Maintenance...");

    uint64_t now = std::time(nullptr);
    int modified_count = 0;

    // 1. Facts Maintenance (Legacy stability-based pruning)
    const char* select_sql = "SELECT key, stability, last_accessed FROM facts WHERE priority = 0;";
    sqlite3_stmt* stmt = nullptr;
    std::vector<std::string> keys_to_prune;

    if (sqlite3_prepare_v2(m_db, select_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* key_ptr = sqlite3_column_text(stmt, 0);
            if (!key_ptr) continue;
            double initial_stability = sqlite3_column_double(stmt, 1);
            uint64_t last_accessed = sqlite3_column_int64(stmt, 2);

            double current_stability = (now > last_accessed) ? 
                initial_stability * std::exp(-m_lambda * (now - last_accessed)) : initial_stability;

            if (current_stability < 0.1) keys_to_prune.push_back(reinterpret_cast<const char*>(key_ptr));
        }
    }
    sqlite3_finalize(stmt);

    if (!keys_to_prune.empty()) {
        sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        const char* del_sql = "DELETE FROM facts WHERE key = ?;";
        sqlite3_stmt* del_stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, del_sql, -1, &del_stmt, nullptr) == SQLITE_OK) {
            for (const auto& key : keys_to_prune) {
                sqlite3_bind_text(del_stmt, 1, key.c_str(), -1, SQLITE_STATIC);
                sqlite3_step(del_stmt);
                sqlite3_reset(del_stmt);
                modified_count++;
            }
        }
        sqlite3_finalize(del_stmt);
        sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    }

    // 2. Summaries Maintenance (Rule v2.1: Active -> Cold -> Forgotten)
    // Phase 6.1: Chunked Scan (100 items per tick)
    const char* summ_sql = "SELECT id, last_accessed, state_enum, recall_count FROM summaries WHERE state_enum < 4 LIMIT 100;";
    if (sqlite3_prepare_v2(m_db, summ_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        struct StateUpdate { int id; int new_state; };
        std::vector<StateUpdate> updates;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            uint64_t last_acc = sqlite3_column_int64(stmt, 1);
            int state = sqlite3_column_int(stmt, 2);
            int recall = sqlite3_column_int(stmt, 3);

            uint64_t idle = now - last_acc;
            int next_state = state;

            if (state == 0 && idle > 86400 * 3) next_state = 1; // Active -> Cold (3 days)
            else if (state == 1 && idle > 86400 * 7 && recall < 2) next_state = 3; // Cold -> Forgotten (7 days, low recall)
            else if (state == 1 && idle > 86400 * 30) next_state = 2; // Cold -> Archived (30 days)

            if (next_state != state) updates.push_back({id, next_state});
        }
        sqlite3_finalize(stmt);

        if (!updates.empty()) {
            sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
            const char* up_sql = "UPDATE summaries SET state_enum = ? WHERE id = ?;";
            sqlite3_stmt* up_stmt = nullptr;
            if (sqlite3_prepare_v2(m_db, up_sql, -1, &up_stmt, nullptr) == SQLITE_OK) {
                for (const auto& u : updates) {
                    sqlite3_bind_int(up_stmt, 1, u.new_state);
                    sqlite3_bind_int(up_stmt, 2, u.id);
                    sqlite3_step(up_stmt);
                    sqlite3_reset(up_stmt);
                    modified_count++;
                }
            }
            sqlite3_finalize(up_stmt);
            sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
        }
    }

    // 3. Cleanup Tombstoned (state 4)
    sqlite3_exec(m_db, "DELETE FROM summaries WHERE state_enum = 4;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "DELETE FROM vectorized_interactions WHERE state_enum = 4;", nullptr, nullptr, nullptr);

    if (modified_count > 0) {
        LOGI(TAG, "Lifecycle Maintenance complete: %d items transitioned or cleared.", modified_count);
    }
    return modified_count;
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
    
    // Stage 1: FTS5 Keyword Match
    const char* sql = "SELECT content FROM summaries_fts WHERE summaries_fts MATCH ? ORDER BY rank LIMIT 5;";
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

std::vector<std::string> LongTermMemory::searchSemantic(const std::vector<float>& query_embedding, RecallMode mode) {
    if (!m_db || query_embedding.empty()) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    
    struct ScoredContent { std::string content; float score; int state; int id; };
    std::vector<ScoredContent> candidates;

    // Phase 2.1: State-based Filtering
    // FAST: state 0,1 | DEEP: state 0,1,2,3 (partial) | EXPLICIT: state 0,1,2,3
    const char* sql = "SELECT content, embedding, state_enum, id FROM summaries WHERE state_enum < 4;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* content = sqlite3_column_text(stmt, 0);
            const void* blob = sqlite3_column_blob(stmt, 1);
            int bytes = sqlite3_column_bytes(stmt, 1);
            int state = sqlite3_column_int(stmt, 2);
            int id = sqlite3_column_int(stmt, 3);

            // Scope logic
            if (mode == RecallMode::FAST && state > 1) continue;
            // DEEP and EXPLICIT can access Forgotten (3)

            if (content && blob && bytes > 0) {
                const uint16_t* f16_vec = static_cast<const uint16_t*>(blob);
                size_t dim = bytes / 2; 
                std::vector<float> vector(dim);
                for (size_t i = 0; i < dim; ++i) vector[i] = halfToFloat(f16_vec[i]);
                
                float dot = 0.0f, mag_a = 0.0f, mag_b = 0.0f;
                for (size_t i = 0; i < std::min(vector.size(), query_embedding.size()); ++i) {
                    dot += vector[i] * query_embedding[i];
                    mag_a += vector[i] * vector[i];
                    mag_b += query_embedding[i] * query_embedding[i];
                }
                float score = (mag_a > 0 && mag_b > 0) ? dot / (std::sqrt(mag_a) * std::sqrt(mag_b)) : 0.0f;
                
                // Suppression logic: Forgotten memory needs much higher confidence in Deep mode
                if (state == 3 && mode == RecallMode::DEEP && score < 0.9f) continue;

                candidates.push_back({reinterpret_cast<const char*>(content), score, state, id});
            }
        }
    }
    sqlite3_finalize(stmt);

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    
    std::vector<std::string> results;
    uint64_t now = std::time(nullptr);
    for (size_t i = 0; i < candidates.size() && i < 5; ++i) {
        if (candidates[i].score > 0.82f) {
            results.push_back(candidates[i].content);
            
            // Phase 5.3: Temporary Resurrection & Promotion
            const char* update_sql = "UPDATE summaries SET recall_count = recall_count + 1, last_accessed = ?, "
                                     "state_enum = CASE WHEN recall_count >= 5 AND state_enum > 1 THEN 1 ELSE state_enum END "
                                     "WHERE id = ?;";
            sqlite3_stmt* up_stmt = nullptr;
            if (sqlite3_prepare_v2(m_db, update_sql, -1, &up_stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(up_stmt, 1, now);
                sqlite3_bind_int(up_stmt, 2, candidates[i].id);
                sqlite3_step(up_stmt);
                sqlite3_finalize(up_stmt);
            }
        }
    }
    return results;
}

bool LongTermMemory::consolidate(const std::string& summary_text, const std::vector<float>& embedding) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    
    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT INTO summaries (content, embedding, timestamp) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_text(stmt, 1, summary_text.c_str(), -1, SQLITE_STATIC);
    
    if (!embedding.empty()) {
        // Phase 2.1: Semantic Memory uses Float16 (2 bytes per dim)
        std::vector<uint16_t> f16_vector(embedding.size());
        for (size_t i = 0; i < embedding.size(); ++i) {
            f16_vector[i] = floatToHalf(embedding[i]);
        }
        sqlite3_bind_blob(stmt, 2, f16_vector.data(), static_cast<int>(f16_vector.size() * 2), SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    sqlite3_bind_int64(stmt, 3, std::time(nullptr));
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    sqlite3_int64 last_id = sqlite3_last_insert_rowid(m_db);
    sqlite3_finalize(stmt);

    const char* fts_sql = "INSERT INTO summaries_fts (content, content_id) VALUES (?, ?);";
    sqlite3_stmt* fts_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, fts_sql, -1, &fts_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(fts_stmt, 1, summary_text.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(fts_stmt, 2, last_id);
        sqlite3_step(fts_stmt);
        sqlite3_finalize(fts_stmt);
    }

    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
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
        // Phase 2.1: Semantic Indexing uses Float16
        std::vector<uint16_t> f16_vector(embedding.size());
        for (size_t i = 0; i < embedding.size(); ++i) {
            f16_vector[i] = floatToHalf(embedding[i]);
        }
        sqlite3_bind_blob(stmt, 5, f16_vector.data(), static_cast<int>(f16_vector.size() * 2), SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 5);
    }

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

std::vector<LongTermMemory::FileEmbedding> LongTermMemory::getAllFileEmbeddings() {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<FileEmbedding> results;

    const char* sql = "SELECT name, path, embedding_vector FROM file_index WHERE embedding_vector IS NOT NULL;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* name = sqlite3_column_text(stmt, 0);
            const unsigned char* path = sqlite3_column_text(stmt, 1);
            const void* blob = sqlite3_column_blob(stmt, 2);
            int bytes = sqlite3_column_bytes(stmt, 2);

            if (name && path && blob && bytes > 0) {
                FileEmbedding fe;
                fe.name = reinterpret_cast<const char*>(name);
                fe.path = reinterpret_cast<const char*>(path);
                
                // De-quantize Float16 to Float32
                const uint16_t* f16_vec = static_cast<const uint16_t*>(blob);
                size_t dim = bytes / 2;
                fe.vector.resize(dim);
                for (size_t i = 0; i < dim; ++i) {
                    fe.vector[i] = halfToFloat(f16_vec[i]);
                }
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
    const char* sql = "SELECT path FROM file_index WHERE name LIKE ? OR path LIKE ? LIMIT 10;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, like_query.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, like_query.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* path = sqlite3_column_text(stmt, 0);
            if (path) {
                results.push_back(reinterpret_cast<const char*>(path));
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

bool LongTermMemory::storeInteraction(const std::string& content, const std::vector<float>& embedding) {
    if (!m_db) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "INSERT INTO vectorized_interactions (content, embedding, timestamp) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, content.c_str(), -1, SQLITE_STATIC);
    if (!embedding.empty()) {
        sqlite3_bind_blob(stmt, 2, embedding.data(), static_cast<int>(embedding.size() * sizeof(float)), SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    sqlite3_bind_int64(stmt, 3, std::time(nullptr));
    
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

std::vector<std::pair<std::string, std::string>> LongTermMemory::getHistory(int limit, int offset) {
    if (!m_db) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::pair<std::string, std::string>> history;
    const char* sql = "SELECT role, content FROM chat_history ORDER BY id ASC LIMIT ? OFFSET ?;";
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
    return history;
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
