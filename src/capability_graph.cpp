#include "capability_graph.h"
#include <algorithm>

namespace Ronin::Kernel::Reasoning {

void CapabilityGraph::addNode(uint32_t id, const std::string& name) {
    if (getNode(id)) return;
    m_nodes.push_back({id, name, {}});
}

void CapabilityGraph::addEdge(uint32_t source_id, uint32_t target_id, float weight) {
    Node* source = getNode(source_id);
    if (!source) return;

    // Check if edge already exists
    for (auto& edge : source->outgoing_edges) {
        if (edge.target_node_id == target_id) return;
    }

    source->outgoing_edges.push_back({target_id, 0, 0, weight, 0.0f});
}

Node* CapabilityGraph::getNode(uint32_t id) {
    for (auto& node : m_nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

} // namespace Ronin::Kernel::Reasoning
