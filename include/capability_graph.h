#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <memory>

namespace Ronin::Kernel::Reasoning {

struct Edge {
    uint32_t target_node_id;
    
    // Thompson Sampling parameters
    uint32_t success_count;
    uint32_t failure_count;
    float base_weight;

    // Derived probability from Thompson Sampling
    float sampled_probability;
};

struct Node {
    uint32_t id;
    std::string capability_name; // e.g., "SIMD_INT8", "NPU_FLBuffers"
    std::vector<Edge> outgoing_edges;
};

class CapabilityGraph {
public:
    void addNode(uint32_t id, const std::string& name);
    void addEdge(uint32_t source_id, uint32_t target_id, float weight);
    
    Node* getNode(uint32_t id);
    std::vector<Node>& getNodes() { return m_nodes; }

private:
    std::vector<Node> m_nodes;
};

} // namespace Ronin::Kernel::Reasoning
