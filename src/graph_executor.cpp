#include "graph_executor.h"
#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include "ronin_log.h"

#define TAG "RoninGraphExecutor"

namespace Ronin::Kernel::Reasoning {

GraphExecutor::GraphExecutor(CapabilityGraph& graph, GraphStorage& storage) 
    : m_graph(graph), m_storage(storage) {}

GraphExecutor::~GraphExecutor() {
    // Wait for any active sync to finish before destruction
    while (m_is_syncing.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

uint32_t GraphExecutor::selectNextNode(uint32_t current_node_id, float divergence_score) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    Node* current = m_graph.getNode(current_node_id);
    if (!current) {
        LOGE(TAG, "selectNextNode: Current node ID %u not found in graph.", current_node_id);
        return 0;
    }
    if (current->outgoing_edges.empty()) return 0;

    uint32_t best_node = 0;
    float max_sample = -1.0f;

    for (auto& edge : current->outgoing_edges) {
        float sample = m_sampler.sampleBeta(edge.success_count, edge.failure_count);
        float adjusted_score = (sample * edge.base_weight) * (1.0f + divergence_score);

        if (adjusted_score > max_sample) {
            max_sample = adjusted_score;
            best_node = edge.target_node_id;
        }
    }
    return best_node;
}

void GraphExecutor::reportOutcome(uint32_t source_id, uint32_t target_id, bool success, RiskLevel risk) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    Node* source = m_graph.getNode(source_id);
    if (!source) return;

    float eta = calculateLearningRate(risk);

    for (auto& edge : source->outgoing_edges) {
        if (edge.target_node_id == target_id) {
            if (success) {
                edge.success_count += static_cast<uint32_t>(1.0f * eta);
                edge.base_weight += (0.1f * eta);
            } else {
                edge.failure_count += static_cast<uint32_t>(1.0f * eta);
                edge.base_weight -= (0.05f * eta);
            }
            break;
        }
    }

    triggerAsyncSync();
}

float GraphExecutor::calculateLearningRate(RiskLevel risk) {
    switch (risk) {
        case RiskLevel::LOW:     return 1.5f;
        case RiskLevel::MEDIUM:  return 1.0f;
        case RiskLevel::HIGH:    return 0.5f;
        case RiskLevel::EXTREME: return 0.1f;
        default:                 return 1.0f;
    }
}

void GraphExecutor::triggerAsyncSync() {
    if (m_is_syncing.exchange(true)) return;

    if (m_sync_thread.joinable()) {
        m_sync_thread.join();
    }

    m_sync_thread = std::thread([this]() {
        LOGI(TAG, "GraphExecutor: Starting async weight persistence to SQLite...");
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_storage.saveGraph(m_graph);
        }
        LOGI(TAG, "GraphExecutor: Successfully synced weights to L3 Deep-store.");
        m_is_syncing.store(false);
    });
}

} // namespace Ronin::Kernel::Reasoning
