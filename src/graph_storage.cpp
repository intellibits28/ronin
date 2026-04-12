#include "graph_storage.h"
#include <iostream>

namespace Ronin::Kernel::Reasoning {

GraphStorage::GraphStorage(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &m_db) != SQLITE_OK) {
        std::cerr << "Failed to open SQLite database: " << sqlite3_errmsg(m_db) << std::endl;
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
        "  FOREIGN KEY(source_id) REFERENCES nodes(id), "
        "  FOREIGN KEY(target_id) REFERENCES nodes(id));";

    return sqlite3_exec(m_db, schema, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool GraphStorage::loadGraph(CapabilityGraph& graph) {
    // Basic implementation of loading from SQLite...
    // In a real scenario, we'd iterate through rows and call graph.addNode/addEdge.
    return true;
}

bool GraphStorage::saveGraph(const CapabilityGraph& graph) {
    // Basic implementation of saving to SQLite...
    return true;
}

} // namespace Ronin::Kernel::Reasoning
