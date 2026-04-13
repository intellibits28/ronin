#pragma once

#include "intent_engine.h"
#include "capability_graph.h"
#include "graph_executor.h"
#include "memory_manager.h"
#include "checkpoint_engine.h"
#include <string>
#include <vector>
#include <functional>

namespace Ronin::Kernel {

struct KernelContext {
    std::string input;
    float intent_score = 0.0f;
    Reasoning::Node* active_node = nullptr;
    bool requires_action = false;
    int iterations = 0;
};

// Dispatch Registry Types
using IntentHandler = void (*)(KernelContext&, Intent::IntentEngine&);
using ExecHandler = void (*)(KernelContext&, Reasoning::GraphExecutor&);

struct DispatchRegistry {
    IntentHandler intent_processor;
    ExecHandler exec_processor;
};

class RoninKernel {
public:
    RoninKernel(
        Intent::IntentEngine& intent_engine,
        Reasoning::CapabilityGraph& graph,
        Reasoning::GraphExecutor& executor,
        Memory::MemoryManager& memory,
        Checkpoint::CheckpointEngine& checkpoint
    );

    /**
     * Autonomous Tick Loop (Bounded)
     */
    void tick(const std::string& input);

private:
    Intent::IntentEngine& m_intent_engine;
    Reasoning::CapabilityGraph& m_graph;
    Reasoning::GraphExecutor& m_executor;
    Memory::MemoryManager& m_memory;
    Checkpoint::CheckpointEngine& m_checkpoint;

    // Static Dispatch Registry
    static const DispatchRegistry s_registry;

    // Internal Handlers
    static void processIntent(KernelContext& ctx, Intent::IntentEngine& engine);
    static void executePlan(KernelContext& ctx, Reasoning::GraphExecutor& executor);
    
    // Security Gate
    bool can_execute(uint32_t node_id);
};

} // namespace Ronin::Kernel
