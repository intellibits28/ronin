#include "graph_executor.h"
#include <algorithm>
#include <iostream>
#include <thread>
#include <android/log.h>

#define TAG "RoninGraphExecutor"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

namespace Ronin::Kernel::Reasoning {

GraphExecutor::GraphExecutor(CapabilityGraph& graph, GraphStorage& storage) 
    : m_graph(graph), m_storage(storage) {}

/**
 * Thompson Sampling with Divergence-based Exploration
 */
uint32_t GraphExecutor::selectNextNode(uint32_t current_node_id, float divergence_score) {
    Node* current = m_graph.getNode(current_node_id);
    if (!current || current->outgoing_edges.empty()) return 0;

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

/**
 * Elastic Weight Consolidation (EWC) inspired dynamic learning rate.
 * Prevents catastrophic forgetting by scaling the update increment based on risk.
 */
void GraphExecutor::reportOutcome(uint32_t source_id, uint32_t target_id, bool success, RiskLevel risk) {
    Node* source = m_graph.getNode(source_id);
    if (!source) return;

    float eta = calculateLearningRate(risk);

    for (auto& edge : source->outgoing_edges) {
        if (edge.target_node_id == target_id) {
            if (success) {
                // Scaling success/failure by eta to provide soft updates
                edge.success_count += static_cast<uint32_t>(1.0f * eta);
                edge.base_weight += (0.1f * eta); // Progressive weight reinforcement
            } else {
                edge.failure_count += static_cast<uint32_t>(1.0f * eta);
                edge.base_weight -= (0.05f * eta); // Conservative penalization
            }
            break;
        }
    }

    // Trigger async sync to SQLite L3 Deep-store
    triggerAsyncSync();
}

/**
 * Returns a learning rate multiplier based on the risk level.
 * High risk = Lower learning rate (consolidation).
 * Low risk = Higher learning rate (exploration).
 */
float GraphExecutor::calculateLearningRate(RiskLevel risk) {
    switch (risk) {
        case RiskLevel::LOW:     return 1.5f;
        case RiskLevel::MEDIUM:  return 1.0f;
        case RiskLevel::HIGH:    return 0.5f;
        case RiskLevel::EXTREME: return 0.1f;
        default:                 return 1.0f;
    }
}

/**
 * Atomic Async Sync: Persists current RAM weights to SQLite in a background thread.
 * Ensures the graph state is saved before potential LMK events.
 */
void GraphExecutor::triggerAsyncSync() {
    if (m_is_syncing.exchange(true)) return; // Prevent concurrent sync tasks

    std::thread([this]() {
        LOGI("GraphExecutor: Starting async weight persistence to SQLite...");
        if (m_storage.saveGraph(m_graph)) {
            LOGI("GraphExecutor: Successfully synced weights to L3 Deep-store.");
        }
        m_is_syncing.store(false);
    }).detach();
}

} // namespace Ronin::Kernel::Reasoning
