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
    
    LOGW(TAG, "Initiating Rollback for Node %u", node_id);
    
    // Find the state and rollback blackboard
    if (!m_speculative_buffer.empty()) {
        auto spec = m_speculative_buffer.back();
        try {
            auto j = nlohmann::json::parse(spec.original_blackboard_json);
            m_blackboard.storage.clear();
            for (auto& [key, val] : j.items()) {
                m_blackboard.storage[key] = val.get<std::string>();
            }
            LOGI(TAG, "Rollback successful. Blackboard restored.");
        } catch(...) {
            LOGE(TAG, "Rollback FAILED: Corruption in buffered state.");
        }
        m_speculative_buffer.pop_back();
    }
}

std::future<bool> SpeculativeGraphExecutor::optimizeAndDispatch(Ronin::Kernel::CapabilityType type, const std::string& payload, Ronin::Kernel::Execution::ExecutionContextPtr ctx) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (m_speculative_mode) {
        // Snapshot current blackboard state
        nlohmann::json jState;
        for (const auto& [k, v] : m_blackboard.storage) jState[k] = v;
        
        SpeculativeState spec;
        spec.node_id = 0; // Temporary ID
        spec.original_blackboard_json = jState.dump();
        m_speculative_buffer.push_back(spec);
        
        LOGI(TAG, "Speculative state buffered for Capability %d", static_cast<int>(type));
    }
    
    return GraphExecutor::optimizeAndDispatch(type, payload, ctx);
}

} // namespace Ronin::Kernel::Reasoning
