#include "capability_graph.h"
#include <algorithm>

namespace Ronin::Kernel::Reasoning {

void CapabilityGraph::addNode(uint32_t id, const std::string& name) {
    if (m_nodes.find(id) != m_nodes.end()) return;
    m_nodes[id] = {id, name, {}};
}

void CapabilityGraph::addEdge(uint32_t source_id, uint32_t target_id, float weight) {
    auto it = m_nodes.find(source_id);
    if (it == m_nodes.end()) return;

    // Check if edge already exists
    for (auto& edge : it->second.outgoing_edges) {
        if (edge.target_node_id == target_id) return;
    }

    it->second.outgoing_edges.push_back({target_id, 0, 0, weight, 0.0f});
}

Node* CapabilityGraph::getNode(uint32_t id) {
    auto it = m_nodes.find(id);
    if (it != m_nodes.end()) return &(it->second);
    return nullptr;
}

} // namespace Ronin::Kernel::Reasoning
