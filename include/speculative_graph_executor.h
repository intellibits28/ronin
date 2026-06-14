#pragma once
#include "graph_executor.h"
#include <deque>

namespace Ronin::Kernel::Reasoning {

/**
 * v1.5 SpeculativeGraphExecutor - Extends GraphExecutor with rollback capabilities.
 */
class SpeculativeGraphExecutor : public GraphExecutor {
public:
    SpeculativeGraphExecutor(CapabilityGraph& graph, GraphStorage& storage, Memory::LongTermMemory* ltm = nullptr);
    
    // v1.5 Speculative Features
    void enableSpeculation(bool enabled) { m_speculative_mode = enabled; }
    void commitNode(uint32_t node_id);
    void rollbackNode(uint32_t node_id);

    // Overridden dispatch to handle speculative buffering
    std::future<bool> optimizeAndDispatch(CapabilityType type, const std::string& payload, Execution::ExecutionContextPtr ctx);

private:
    bool m_speculative_mode = false;
    
    struct SpeculativeState {
        uint32_t node_id;
        std::string original_blackboard_json;
    };
    
    std::deque<SpeculativeState> m_speculative_buffer;
};

} // namespace Ronin::Kernel::Reasoning
