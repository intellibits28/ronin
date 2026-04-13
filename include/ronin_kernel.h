#pragma once

#include "intent_engine.h"
#include "capability_graph.h"
#include "graph_executor.h"
#include "memory_manager.h"
#include "checkpoint_engine.h"
#include <string>
#include <memory>

namespace Ronin::Kernel {

struct KernelState {
    float intent = 0.0f;
    Reasoning::Node* active_node = nullptr;
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
     * Core heartbeat logic. Processes input and updates kernel state.
     */
    void tick(const std::string& input);

private:
    Intent::IntentEngine& m_intent_engine;
    Reasoning::CapabilityGraph& m_graph;
    Reasoning::GraphExecutor& m_executor;
    Memory::MemoryManager& m_memory;
    Checkpoint::CheckpointEngine& m_checkpoint;
    
    KernelState m_state;
};

} // namespace Ronin::Kernel
