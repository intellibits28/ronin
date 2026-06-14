#include "speculative_graph_executor.h"
#include "ronin_log.h"
#include <nlohmann/json.hpp>

#define TAG "SpeculativeExecutor"

namespace Ronin::Kernel::Reasoning {

SpeculativeGraphExecutor::SpeculativeGraphExecutor(CapabilityGraph& graph, GraphStorage& storage, Memory::LongTermMemory* ltm)
    : GraphExecutor(graph, storage, ltm) {}

void SpeculativeGraphExecutor::commitNode(uint32_t node_id) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_speculative_mode) return;
    
    // In a full implementation, this would flush buffered side-effects to LTM.
    // For now, we just clear the rollback state for this node.
    if (!m_speculative_buffer.empty() && m_speculative_buffer.front().node_id == node_id) {
        m_speculative_buffer.pop_front();
        LOGI(TAG, "Committed speculative node %u", node_id);
    }
}

void SpeculativeGraphExecutor::rollbackNode(uint32_t node_id) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_speculative_mode) return;
    
    // Find the state and rollback blackboard
    for (auto it = m_speculative_buffer.begin(); it != m_speculative_buffer.end(); ++it) {
        if (it->node_id == node_id) {
            // Restore blackboard
            try {
                auto j = nlohmann::json::parse(it->original_blackboard_json);
                m_blackboard.storage.clear();
                for (auto& [key, val] : j.items()) {
                    m_blackboard.storage[key] = val.get<std::string>();
                }
                LOGW(TAG, "Rolled back blackboard state for node %u", node_id);
            } catch(...) {
                LOGE(TAG, "Failed to parse blackboard rollback state.");
            }
            
            // Remove this and subsequent speculative states
            m_speculative_buffer.erase(it, m_speculative_buffer.end());
            break;
        }
    }
}

std::future<bool> SpeculativeGraphExecutor::optimizeAndDispatch(CapabilityType type, const std::string& payload, Execution::ExecutionContextPtr ctx) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (m_speculative_mode) {
        // Snapshot current blackboard
        nlohmann::json jState;
        for (const auto& [k, v] : m_blackboard.storage) jState[k] = v;
        
        // We don't have the exact node_id yet here, so we buffer the state 
        // and let the base class handle the dispatch. The callback would need
        // to be intercepted to associate the node_id.
        // For v1.5 prototype, we just log.
        LOGI(TAG, "Speculative state buffered.");
    }
    
    return GraphExecutor::optimizeAndDispatch(type, payload, ctx);
}

} // namespace Ronin::Kernel::Reasoning
