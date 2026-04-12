#include "graph_executor.h"
#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include <cctype>
#include "ronin_log.h"

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

/**
 * Foolproof Version: Explicit Keyword Bypass + Thompson Sampling
 */
Node* GraphExecutor::selectNextNode(const std::string& input) {
    // 1. Exact Log: See what C++ receives
    // We use our unified LOGI or the requested __android_log_print
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "RONIN_KERN", "Raw Input: '%s'", input.c_str());
#else
    printf("[RONIN_KERN] DEBUG: Raw Input: '%s'\n", input.c_str());
#endif
    
    // 2. Normalize: Trim and Lowercase
    std::string clean_input = trim(lowercase(input));
    
    // 3. Absolute Override (The Nuclear Path)
    if (clean_input.find("search") != std::string::npos || 
        clean_input.find("find") != std::string::npos) {
        
        LOGI(TAG, "> CRITICAL BYPASS: Forced FileSearchNode");
        return m_graph.getNodeByID("FileSearchNode");
    }

    // 4. Only if no keywords, proceed to Thompson Sampling
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Default to Reasoning_Engine (ID 1) as entry point
    Node* current = m_graph.getNode(1); 
    if (!current) {
        LOGE(TAG, "Thompson Sampling: Root node (ID 1) not found.");
        return nullptr;
    }

    if (current->outgoing_edges.empty()) return current;

    uint32_t best_node_id = 0;
    float max_sample = -1.0f;

    for (auto& edge : current->outgoing_edges) {
        float sample = m_sampler.sampleBeta(edge.success_count, edge.failure_count);
        
        // Use a static divergence score for now
        float adjusted_score = (sample * edge.base_weight) * 1.5f;

        if (adjusted_score > max_sample) {
            max_sample = adjusted_score;
            best_node_id = edge.target_node_id;
        }
    }

    Node* result = m_graph.getNode(best_node_id);
    return result ? result : current;
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
