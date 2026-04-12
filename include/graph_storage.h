#pragma once

#include "capability_graph.h"
#include <sqlite3.h>

namespace Ronin::Kernel::Reasoning {

class GraphStorage {
public:
    GraphStorage(const std::string& db_path);
    ~GraphStorage();

    bool loadGraph(CapabilityGraph& graph);
    bool saveGraph(const CapabilityGraph& graph);

private:
    sqlite3* m_db = nullptr;
    bool initSchema();
};

} // namespace Ronin::Kernel::Reasoning
