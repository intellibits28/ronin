#include "graph_executor.h"
#include "execution_checkpoint_store.h"
#include "adaptive_budget_controller.h"
#include "failure_telemetry_bus.h"
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

constexpr float MIN_CAPABILITY_CONFIDENCE = 0.5f;
constexpr float MIN_ENTITY_CONFIDENCE = 0.3f;

namespace Ronin::Kernel::Reasoning {

GraphExecutor::GraphExecutor(CapabilityGraph& graph, GraphStorage& storage, Memory::LongTermMemory* ltm)
    : m_graph(graph), m_storage(storage), m_ltm(ltm), m_belief_state(ltm), m_reflection_engine(ltm, &m_sampler) {
    
    // Wire up RLHF feedback from UI back into the DAG Edge Weights
    // Using [this] is safe here because m_reflection_engine is a member of GraphExecutor and its lifetime is tied to it.
    m_reflection_engine.setWeightUpdateCallback([this](const std::string& session_id, bool was_helpful) {
        LOGI(TAG, "Applying RLHF weight update for session %s (Helpful: %d)", session_id.c_str(), was_helpful);
        
        uint32_t target_node_id = 1; // Default to Root/Chat
        if (m_ltm) {
            // TODO (Production): Implement a specific query in LTM to fetch the node_id for the given session_id.
            // Example: target_node_id = m_ltm->getEpisodeNodeId(session_id);
            // For now, we keep the fallback demonstration logic.
        }
        
        // Boost Root -> Target Node based on user satisfaction.
        this->reportOutcome(1, target_node_id, was_helpful, RiskLevel::LOW);
    });
}

GraphExecutor::~GraphExecutor() {
    // Clear callback to prevent dangling pointer access just in case
    m_reflection_engine.setWeightUpdateCallback(nullptr);
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
    const uint32_t DECAY_THRESHOLD = 10; // Prevent premature integer collapse

    for (auto& edge : source->outgoing_edges) {
        // Phase 3 (Task 5): Symmetric Decay to prevent "history prison" in Thompson Sampling.
        // Applies a gentle 10% decay only when counts build up, ensuring dynamic exploration over time.
        if (edge.success_count + edge.failure_count > DECAY_THRESHOLD) {
            edge.success_count = static_cast<uint32_t>(std::ceil(edge.success_count * 0.9f));
            edge.failure_count = static_cast<uint32_t>(std::ceil(edge.failure_count * 0.9f));
        }

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

std::future<bool> GraphExecutor::optimizeAndDispatch(CapabilityType type, const std::string& payload, Execution::ExecutionContextPtr ctx) {
    auto promise = std::make_shared<std::promise<bool>>();
    auto completed = std::make_shared<std::atomic<bool>>(false);
    std::future<bool> future = promise->get_future();

    if (ctx && ctx->cancel_token && ctx->cancel_token->isCancelled()) {
        LOGE(TAG, "L10: Execution cancelled before dispatch.");
        promise->set_value(false);
        return future;
    }

    // Checking if safe mode is enabled via LongTermMemory status (as a proxy for global state for now)
    if (m_ltm && m_ltm->isReadOnly()) {
        LOGE(TAG, "L10: Execution blocked by SafeMode.");
        promise->set_value(false);
        return future;
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    // v10.2.14: Update active session tracking
    m_current_session_id = ctx ? ctx->session_id : "unknown";
    
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
    req.session_id = ctx ? ctx->session_id : "unknown";
    req.exec_context = ctx;
    
    // Inject all blackboard data into payload
    nlohmann::json jPayload = nlohmann::json::parse(payload.empty() ? "{}" : payload);
    for (const auto& [key, val] : m_blackboard.storage) jPayload["context_" + key] = val;

    // Phase 3 (Task 4): Integrate Belief State to allow world-knowledge-aware planning
    auto capability_belief = m_belief_state.getBelief(type_str);
    if (capability_belief.confidence > MIN_CAPABILITY_CONFIDENCE && !capability_belief.value.empty()) {
        jPayload["belief_context"] = capability_belief.value;
    }
    
    // Check if the payload specifies any named entity to resolve from beliefs
    if (jPayload.contains("target_entity")) {
        std::string entity = jPayload["target_entity"].get<std::string>();
        auto entity_belief = m_belief_state.getBelief(entity);
        if (entity_belief.confidence > MIN_ENTITY_CONFIDENCE && !entity_belief.value.empty()) {
            jPayload["belief_" + entity] = entity_belief.value;
        }
    }

    req.payload_json = jPayload.dump();

    LOGI(TAG, "L10 Optimizer: Selected Node %u for Capability %d. Dispatching...", 
         best_node_id, static_cast<int>(type));
         
    CapabilityDispatcher::getInstance().dispatch(req, [this, best_node_id, promise, completed, type, ctx](const CapabilityResponse& res) {
        std::string sid = ctx ? ctx->session_id : "unknown";
        if (completed->exchange(true)) {
            LOGW(TAG, "L10: Duplicate response ignored for session %s.", sid.c_str());
            return;
        }

        // v7.4: Feedback + Blackboard Storage
        this->reportOutcome(0, best_node_id, res.success, RiskLevel::MEDIUM);
        
        if (res.success) {
            std::lock_guard<std::recursive_mutex> inner_lock(m_mutex);
            
            // v10.2.14: Isolation guard - only update blackboard if session is still active
            if (sid != m_current_session_id) {
                LOGW(TAG, "L10: Tool result arrived for STALE session %s. Dropping.", sid.c_str());
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
