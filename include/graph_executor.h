#pragma once

#include "capability_graph.h"
#include "thompson_sampler.h"
#include "graph_storage.h"
#include <future>
#include <atomic>
#include <mutex>

namespace Ronin::Kernel::Reasoning {

enum class RiskLevel {
    LOW,      // Exploration phase, high learning rate
    MEDIUM,   // Standard operation
    HIGH,     // Critical path, conservative updates (EWC-like)
    EXTREME   // Minimum updates to prevent catastrophic forgetting
};

class GraphExecutor {
public:
    GraphExecutor(CapabilityGraph& graph, GraphStorage& storage);

    // Selects the next node ID to execute
    uint32_t selectNextNode(uint32_t current_node_id, float divergence_score);

    // Feedback loop with dynamic learning rate based on risk level
    void reportOutcome(uint32_t source_id, uint32_t target_id, bool success, RiskLevel risk);

    // Forces an async sync to the L3 Deep-store
    void triggerAsyncSync();

private:
    CapabilityGraph& m_graph;
    GraphStorage& m_storage;
    ThompsonSampler m_sampler;
    
    std::mutex m_mutex;
    std::atomic<bool> m_is_syncing{false};
    
    // Dynamic learning rate helper
    float calculateLearningRate(RiskLevel risk);
};

} // namespace Ronin::Kernel::Reasoning
