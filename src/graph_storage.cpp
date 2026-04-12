#include "graph_storage.h"
#include <vector>
#include "ronin_log.h"
#include <iostream>

#define TAG "RoninGraphStorage"

namespace Ronin::Kernel::Reasoning {

GraphStorage::GraphStorage(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &m_db) != SQLITE_OK) {
        LOGE(TAG, "Failed to open SQLite database: %s", sqlite3_errmsg(m_db));
    } else {
        initSchema();
    }
}

GraphStorage::~GraphStorage() {
    if (m_db) sqlite3_close(m_db);
}

bool GraphStorage::initSchema() {
    const char* schema = 
        "CREATE TABLE IF NOT EXISTS nodes (id INTEGER PRIMARY KEY, name TEXT);"
        "CREATE TABLE IF NOT EXISTS edges ("
        "  source_id INTEGER, target_id INTEGER, "
        "  success_count INTEGER DEFAULT 0, failure_count INTEGER DEFAULT 0, "
        "  base_weight REAL DEFAULT 1.0,"
        "  PRIMARY KEY(source_id, target_id),"
        "  FOREIGN KEY(source_id) REFERENCES nodes(id), "
        "  FOREIGN KEY(target_id) REFERENCES nodes(id));";

    if (sqlite3_exec(m_db, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to initialize Graph schema: %s", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

bool GraphStorage::loadGraph(CapabilityGraph& graph) {
    if (!m_db) return false;
    const char* node_sql = "SELECT id, name FROM nodes;";
    sqlite3_stmt* n_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, node_sql, -1, &n_stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(n_stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(n_stmt, 0);
            const unsigned char* name_ptr = sqlite3_column_text(n_stmt, 1);
            std::string name = name_ptr ? reinterpret_cast<const char*>(name_ptr) : "Unknown_Capability";
            graph.addNode(static_cast<uint32_t>(id), name);
        }
    }
    sqlite3_finalize(n_stmt);

    const char* edge_sql = "SELECT source_id, target_id, success_count, failure_count, base_weight FROM edges;";
    sqlite3_stmt* e_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, edge_sql, -1, &e_stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(e_stmt) == SQLITE_ROW) {
            uint32_t src = sqlite3_column_int(e_stmt, 0);
            uint32_t target = sqlite3_column_int(e_stmt, 1);
            graph.addEdge(src, target, static_cast<float>(sqlite3_column_double(e_stmt, 4)));

            // Populate weights/counts safely
            Node* s_node = graph.getNode(src);
            if (s_node) {
                for (auto& edge : s_node->outgoing_edges) {
                    if (edge.target_node_id == target) {
                        edge.success_count = static_cast<uint32_t>(sqlite3_column_int(e_stmt, 2));
                        edge.failure_count = static_cast<uint32_t>(sqlite3_column_int(e_stmt, 3));
                        break;
                    }
                }
            }
        }
        sqlite3_finalize(e_stmt);
    }
 else {
        LOGE(TAG, "Failed to prepare edge loading query: %s", sqlite3_errmsg(m_db));
    }
    LOGI(TAG, "Capability Graph successfully loaded from SQLite Deep-store.");
    return true;
}

bool GraphStorage::saveGraph(const CapabilityGraph& graph) {
    if (!m_db) return false;
    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* n_sql = "INSERT OR REPLACE INTO nodes (id, name) VALUES (?, ?);";
    sqlite3_stmt* n_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, n_sql, -1, &n_stmt, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to prepare node save query: %s", sqlite3_errmsg(m_db));
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    const char* e_sql = "INSERT OR REPLACE INTO edges (source_id, target_id, success_count, failure_count, base_weight) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* e_stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, e_sql, -1, &e_stmt, nullptr) != SQLITE_OK) {
        LOGE(TAG, "Failed to prepare edge save query: %s", sqlite3_errmsg(m_db));
        sqlite3_finalize(n_stmt);
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    for (const auto& [id, node] : graph.getNodes()) {
        sqlite3_bind_int(n_stmt, 1, node.id);
        sqlite3_bind_text(n_stmt, 2, node.capability_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(n_stmt);
        sqlite3_reset(n_stmt);

        for (const auto& edge : node.outgoing_edges) {
            sqlite3_bind_int(e_stmt, 1, node.id);
            sqlite3_bind_int(e_stmt, 2, edge.target_node_id);
            sqlite3_bind_int(e_stmt, 3, edge.success_count);
            sqlite3_bind_int(e_stmt, 4, edge.failure_count);
            sqlite3_bind_double(e_stmt, 5, static_cast<double>(edge.base_weight));
            sqlite3_step(e_stmt);
            sqlite3_reset(e_stmt);
        }
    }

    sqlite3_finalize(n_stmt);
    sqlite3_finalize(e_stmt);
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    LOGI(TAG, "Capability Graph successfully saved to SQLite Deep-store.");
    return true;
}

} // namespace Ronin::Kernel::Reasoning
