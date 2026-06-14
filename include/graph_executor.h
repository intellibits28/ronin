#pragma once

#include "capability_graph.h"
#include "thompson_sampler.h"
#include "graph_storage.h"
#include "ronin_types.hpp"
#include "capability_dispatcher.h"
#include "long_term_memory.h"
#include "belief_state.h"
#include "reflection_engine.h"
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
    GraphExecutor(CapabilityGraph& graph, GraphStorage& storage, Memory::LongTermMemory* ltm = nullptr);
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
     * v7.0 Layer 10: Optimizes and executes a capability request using Thompson Sampling.
     */
    std::future<bool> optimizeAndDispatch(CapabilityType type, const std::string& payload, Execution::ExecutionContextPtr ctx);

    /**
     * v7.0: Executes a deterministic sequence of nodes (Dynamic Tool Chaining).
     */
    std::string executeChain(const std::vector<uint32_t>& steps, 
                             const std::string& input,
                             std::function<std::string(uint32_t, const std::string&, ToolContext*)> skill_executor);

    // Forces an async sync to the L3 Deep-store
    void triggerAsyncSync();

    // v10.2.13: Isolation logic
    void clearContext();

    /**
     * v13.0: Records a task execution in episodic memory with cognitive context.
     */
    void recordEpisode(const std::string& intent, const std::string& summary, const std::string& payload, 
                       bool success, const std::string& goal_id = "", const std::string& node_id = "",
                       int64_t latency_ms = 0, float conf_before = 0.0f, float conf_after = 0.0f);

    /**
     * v13.0: Records a prediction (Expectation vs Reality) for reflection.
     */
    void recordPrediction(const std::string& goal_id, const std::string& node_id,
                          const std::string& predicted_json, const std::string& actual_json, float error_score);

    BeliefState& getBeliefState() { return m_belief_state; }
    ReflectionEngine& getReflectionEngine() { return m_reflection_engine; }

    // v1.5 Self-Healing Toggle
    void setRetryMode(bool enabled) { m_retry_graph_mode = enabled; }

protected:
    CapabilityGraph& m_graph;
    GraphStorage& m_storage;
    Memory::LongTermMemory* m_ltm;
    ThompsonSampler m_sampler;
    BeliefState m_belief_state;           // v13.0
    ReflectionEngine m_reflection_engine; // v13.0
    ToolContext m_blackboard; // v7.0 Shared Memory (Blackboard)
    
    std::recursive_mutex m_mutex;
    std::atomic<bool> m_is_syncing{false};
    std::thread m_sync_thread;
    std::string m_current_session_id; // v10.2.14: Session isolation
    bool m_retry_graph_mode = true; // v1.5 Default enabled

private:
    // Dynamic learning rate helper
    float calculateLearningRate(RiskLevel risk);

    // Thompson Sampling logic
    Node* runThompsonSampling(const std::string& input);
};

} // namespace Ronin::Kernel::Reasoning
