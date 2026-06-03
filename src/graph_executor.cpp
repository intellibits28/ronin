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

GraphExecutor::GraphExecutor(CapabilityGraph& graph, GraphStorage& storage) 
    : m_graph(graph), m_storage(storage) {}

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
    
    // v7.0 Layer 10 logic: Redundancy Optimization
    uint32_t best_node_id = 1; // Default
    float max_sample = -1.0f;
    
    for (auto& [id, node] : m_graph.getNodes()) {
        float sample = m_sampler.sampleBeta(10, 1);
        if (sample > max_sample) {
            max_sample = sample;
            best_node_id = id;
        }
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
         
    CapabilityDispatcher::getInstance().dispatch(req, [this, best_node_id, promise](const CapabilityResponse& res) {
        // v7.4: Feedback + Blackboard Storage
        this->reportOutcome(0, best_node_id, res.success, RiskLevel::MEDIUM);
        
        if (res.success) {
            std::lock_guard<std::recursive_mutex> inner_lock(m_mutex);
            std::string cap_key = "last_result";
            m_blackboard.storage[cap_key] = res.payload_json;
            LOGI(TAG, "L10: Blackboard updated with result from Node %u", best_node_id);
        }
        
        promise->set_value(res.success);
    });

    return future;
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

} // namespace Ronin::Kernel::Reasoning
