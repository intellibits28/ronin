#include "graph_executor.h"
#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include <cctype>
#include <cstring>
#include <nlohmann/json.hpp>
#include "ronin_log.h"

#ifdef ANDROID
#include <android/log.h>
#endif

#define TAG "RoninGraphExecutor"

namespace Ronin::Kernel::Reasoning {

GraphExecutor::GraphExecutor(CapabilityGraph& graph, GraphStorage& storage, Memory::LongTermMemory* ltm)
    : m_graph(graph), m_storage(storage), m_ltm(ltm) {}

GraphExecutor::~GraphExecutor() {
    if (m_sync_thread.joinable()) {
        m_sync_thread.join();
    }
}

GraphExecutor::TaskPlan GraphExecutor::generatePlan(const std::string& input_text) {
    TaskPlan plan;
    uint32_t current_id = 1; // Always start at Root/Chat
    plan.steps.push_back(current_id);

    // v7.0 Layer 10: Deep-Path Projection (Recursive strategy discovery)
    for (int i = 0; i < 5; ++i) {
        Node* current_node = m_graph.getNode(current_id);
        if (!current_node || current_node->outgoing_edges.empty()) break;

        uint32_t best_next = current_id;
        float max_sample = -1.0f;

        for (const auto& edge : current_node->outgoing_edges) {
            float sample = m_sampler.sampleBeta(edge.success_count + 1, edge.failure_count + 1);
            if (sample > max_sample) {
                max_sample = sample;
                best_next = edge.target_node_id;
            }
        }

        if (best_next == current_id) break;
        plan.steps.push_back(best_next);
        current_id = best_next;
    }
    
    return plan;
}

void GraphExecutor::reportOutcome(uint32_t source_id, uint32_t target_id, bool success, RiskLevel risk) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Node* source = m_graph.getNode(source_id);
    if (!source) return;

    float eta = calculateLearningRate(risk);
    const float decay = 0.95f; // Forgetting factor to keep sampling fresh

    for (auto& edge : source->outgoing_edges) {
        // Apply global decay
        edge.success_count = static_cast<uint32_t>(edge.success_count * decay);
        edge.failure_count = static_cast<uint32_t>(edge.failure_count * decay);

        if (edge.target_node_id == target_id) {
            if (success) {
                edge.success_count += static_cast<uint32_t>(1.0f * eta);
                edge.base_weight += (0.1f * eta);
            } else {
                edge.failure_count += static_cast<uint32_t>(1.0f * eta);
                edge.base_weight -= (0.05f * eta);
            }
        }
    }

    triggerAsyncSync();
}

std::future<bool> GraphExecutor::optimizeAndDispatch(CapabilityType type, const std::string& session_id, const std::string& payload) {
    auto promise = std::make_shared<std::promise<bool>>();
    std::future<bool> future = promise->get_future();

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    // v10.2.14: Update active session tracking
    m_current_session_id = session_id;
    
    // v10.1.19: Targeted Node Selection
    uint32_t best_node_id = 1; // Default
    float max_sample = -1.0f;
    std::string type_str = Ronin::Kernel::CapabilityTypeToString(type);
    
    bool found_match = false;
    for (auto& [id, node] : m_graph.getNodes()) {
        // v11.3: Match node name to capability type (e.g. MemoryNode matches MEMORY)
        std::string n_upper = node.capability_name;
        std::transform(n_upper.begin(), n_upper.end(), n_upper.begin(), ::toupper);
        
        if (n_upper.find(type_str) != std::string::npos || (type == CapabilityType::NONE)) {
            float sample = m_sampler.sampleBeta(node.outgoing_edges.empty() ? 10 : 0, 1); // Mock weights for now
            if (sample > max_sample) {
                max_sample = sample;
                best_node_id = id;
                found_match = true;
            }
        }
    }

    if (!found_match) {
        LOGW(TAG, "L10: No specialized node found for capability %s. Using default Node 1.", type_str.c_str());
    }

    // v7.0: Construct Request with Blackboard data injection
    CapabilityRequest req;
    req.request_id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    req.capability = type;
    req.session_id = session_id;
    
    // Inject all blackboard data into payload
    nlohmann::json jPayload = nlohmann::json::parse(payload.empty() ? "{}" : payload);
    for (const auto& [key, val] : m_blackboard.storage) jPayload["context_" + key] = val;
    req.payload_json = jPayload.dump();

    LOGI(TAG, "L10 Optimizer: Selected Node %u for Capability %d. Dispatching...", 
         best_node_id, static_cast<int>(type));
         
    CapabilityDispatcher::getInstance().dispatch(req, [this, best_node_id, promise, type, session_id](const CapabilityResponse& res) {
        // v7.4: Feedback + Blackboard Storage
        this->reportOutcome(0, best_node_id, res.success, RiskLevel::MEDIUM);
        
        if (res.success) {
            std::lock_guard<std::recursive_mutex> inner_lock(m_mutex);
            
            // v10.2.14: Isolation guard - only update blackboard if session is still active
            if (session_id != m_current_session_id) {
                LOGW(TAG, "L10: Tool result arrived for STALE session %s. Dropping.", session_id.c_str());
            } else {
                // v9.2: Key results by capability name to avoid overwrites
                std::string cap_str = Ronin::Kernel::CapabilityTypeToString(type);
                m_blackboard.storage["result_" + cap_str] = res.payload_json;
                LOGI(TAG, "L10: Blackboard updated with result from %s", cap_str.c_str());
            }
        }
        
        promise->set_value(res.success);
    });

    return future;
}

void GraphExecutor::recordEpisode(const std::string& intent, const std::string& summary, const std::string& payload, 
                                  bool success, const std::string& goal_id, const std::string& node_id,
                                  int64_t latency_ms, float conf_before, float conf_after) {
    if (m_ltm) {
        m_ltm->storeEpisode(intent, summary, payload, success, goal_id, node_id, latency_ms, conf_before, conf_after);
    }
}

void GraphExecutor::recordPrediction(const std::string& goal_id, const std::string& node_id,
                                     const std::string& predicted_json, const std::string& actual_json, float error_score) {
    if (m_ltm) {
        m_ltm->storePrediction(goal_id, node_id, predicted_json, actual_json, error_score);
    }
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
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_storage.saveGraph(m_graph);
        }
        LOGI(TAG, "GraphExecutor: Successfully synced weights to L3 Deep-store.");
        m_is_syncing.store(false);
    });
}

void GraphExecutor::clearContext() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_blackboard.storage.clear();
    LOGI(TAG, "L10: Blackboard cleared (Session Isolation).");
}

} // namespace Ronin::Kernel::Reasoning
