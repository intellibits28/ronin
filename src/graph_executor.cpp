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

// --- Helpers for String Normalization ---

static std::string lowercase(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

static std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) {
        start++;
    }
    if (start == s.end()) return "";
    auto end = s.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(*end));
    return std::string(start, end + 1);
}

// --- GraphExecutor Implementation ---

GraphExecutor::GraphExecutor(CapabilityGraph& graph, GraphStorage& storage, Memory::LongTermMemory* ltm) 
    : m_graph(graph), m_storage(storage), m_ltm(ltm), m_sampler() {}

GraphExecutor::~GraphExecutor() {
    if (m_sync_thread.joinable()) {
        m_sync_thread.join();
    }
}

GraphExecutor::TaskPlan GraphExecutor::generatePlan(const std::string& input) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    TaskPlan plan;
    
    // v6.0: Multi-step Path Exploration (Max Depth 4)
    uint32_t current_id = 1; 
    
    for (int i = 0; i < 4; ++i) {
        Node* current = m_graph.getNode(current_id);
        if (!current || current->outgoing_edges.empty()) break;

        uint32_t best_next = current_id;
        float max_sample = -1.0f;

        for (auto& edge : current->outgoing_edges) {
            float sample = m_sampler.sampleBeta(edge.success_count, edge.failure_count);
            float score = sample * edge.base_weight;

            if (score > max_sample) {
                max_sample = score;
                best_next = edge.target_node_id;
            }
        }

        if (best_next == current_id) break;
        plan.steps.push_back(best_next);
        current_id = best_next;
    }
    
    // Default to Chat (1) if no steps found
    if (plan.steps.empty()) plan.steps.push_back(1);
    
    LOGI(TAG, "Adaptive Plan Generated: %zu steps.", plan.steps.size());
    return plan;
}

std::string GraphExecutor::executeChain(const std::vector<uint32_t>& steps, 
                                       const std::string& input,
                                       std::function<std::string(uint32_t, const std::string&, ToolContext*)> skill_executor) {
    std::string final_result;
    m_blackboard.storage.clear(); // Fresh start for each chain
    
    LOGI(TAG, "v7.0: Starting dynamic tool chain execution (%zu steps).", steps.size());
    
    for (uint32_t nodeId : steps) {
        LOGI(TAG, "> Executing Node ID %u in chain.", nodeId);
        std::string result = skill_executor(nodeId, input, &m_blackboard);
        
        // Append result to final output
        if (!final_result.empty()) final_result += "\n";
        final_result += result;
        
        // Optional: Termination logic if a critical node fails
        if (result.find("Error:") != std::string::npos) {
            LOGW(TAG, "Node ID %u failed. Breaking chain.", nodeId);
            break;
        }
    }
    
    return final_result;
}


std::future<bool> GraphExecutor::optimizeAndDispatch(CapabilityType type, const std::string& session_id, const std::string& payload) {
    auto promise = std::make_shared<std::promise<bool>>();
    std::future<bool> future = promise->get_future();

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
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
         
    CapabilityDispatcher::getInstance().dispatch(req, [this, best_node_id, promise, type](const CapabilityResponse& res) {
        // v7.4: Feedback + Blackboard Storage
        this->reportOutcome(0, best_node_id, res.success, RiskLevel::MEDIUM);
        
        if (res.success) {
            std::lock_guard<std::recursive_mutex> inner_lock(m_mutex);
            // v9.2: Key results by capability name to avoid overwrites
            std::string cap_str = Ronin::Kernel::CapabilityTypeToString(type);
            m_blackboard.storage["result_" + cap_str] = res.payload_json;
            LOGI(TAG, "L10: Blackboard updated with result from %s", cap_str.c_str());
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
        LOGI(TAG, "L10: Episodic memory recorded (v13.0) for intent: %s", intent.c_str());
    }
}

void GraphExecutor::recordPrediction(const std::string& goal_id, const std::string& node_id,
                                     const std::string& predicted_json, const std::string& actual_json, float error_score) {
    if (m_ltm) {
        m_ltm->storePrediction(goal_id, node_id, predicted_json, actual_json, error_score);
        LOGI(TAG, "L10: Prediction recorded (v13.0) for node: %s", node_id.c_str());
    }
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
