#pragma once

#include "capability_graph.h"
#include "thompson_sampler.h"
#include "graph_storage.h"
#include "ronin_types.hpp"
#include <future>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>

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
    ~GraphExecutor();

    // Execution Plan for multi-step tasks
    struct TaskPlan {
        std::vector<uint32_t> steps;
        std::unordered_map<std::string, std::string> shared_context;
    };

    // Generates a multi-step execution plan based on input and learned weights
    TaskPlan generatePlan(const std::string& input_text);

    // Feedback loop with dynamic learning rate based on risk level
    void reportOutcome(uint32_t source_id, uint32_t target_id, bool success, RiskLevel risk);

    /**
     * v7.0: Executes a deterministic sequence of nodes (Dynamic Tool Chaining).
     */
    std::string executeChain(const std::vector<uint32_t>& steps, 
                             const std::string& input,
                             std::function<std::string(uint32_t, const std::string&, ToolContext*)> skill_executor);

    // Forces an async sync to the L3 Deep-store
    void triggerAsyncSync();

private:
    CapabilityGraph& m_graph;
    GraphStorage& m_storage;
    ThompsonSampler m_sampler;
    ToolContext m_blackboard; // v7.0 Shared Memory (Blackboard)
    
    std::mutex m_mutex;
    std::atomic<bool> m_is_syncing{false};
    std::thread m_sync_thread;
    
    // Dynamic learning rate helper
    float calculateLearningRate(RiskLevel risk);

    // Thompson Sampling logic
    Node* runThompsonSampling(const std::string& input);
};

} // namespace Ronin::Kernel::Reasoning
