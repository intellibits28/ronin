#include "graph_executor.h"
#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include <cctype>
#include <cstring>
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

Node* GraphExecutor::selectNextNode(const std::string& input) {
    std::string clean = trim(lowercase(input));
    
    // --- STRICT HARDWARE BYPASS v3.9.7-AUTO-SCROLL ---
    bool isOff = (clean.find("off") != std::string::npos || 
                  clean.find("stop") != std::string::npos || 
                  clean.find("disable") != std::string::npos);

    bool hasVerb = (clean.find("turn on") != std::string::npos || clean.find("turn off") != std::string::npos || 
                    clean.find("switch on") != std::string::npos || clean.find("switch off") != std::string::npos ||
                    clean.find("enable") != std::string::npos || clean.find("disable") != std::string::npos);

    if (hasVerb) {
        if (clean.find("flashlight") != std::string::npos || clean.find("torch") != std::string::npos || clean.find("light") != std::string::npos) {
            LOGI(TAG, "> Route: Strict Bypass (Intent: Flashlight) [v3.9.7-AUTO-SCROLL]");
            return m_graph.getNode(4);
        }
        if (clean.find("wifi") != std::string::npos) {
            LOGI(TAG, "> Route: Strict Bypass (Intent: WiFi) [v3.9.7-AUTO-SCROLL]");
            return m_graph.getNode(6);
        }
        if (clean.find("bluetooth") != std::string::npos || clean.find("bt") != std::string::npos) {
            LOGI(TAG, "> Route: Strict Bypass (Intent: Bluetooth) [v3.9.7-AUTO-SCROLL]");
            return m_graph.getNode(7);
        }
    }

    if (clean == "where am i" || clean == "get location" || clean == "gps status") {
        LOGI(TAG, "> Route: Strict Bypass (Intent: Location) [v3.9.7-AUTO-SCROLL]");
        return m_graph.getNode(5);
    }

    // --- SEARCH BYPASS ---
    bool isSearchPrefix = (clean.find("search ") == 0 || clean.find("find ") == 0 || clean.find("locate ") == 0);
    bool isFilename = (clean.find(".pdf") != std::string::npos || clean.find(".txt") != std::string::npos || 
                       clean.find(".jpg") != std::string::npos || clean.find(".jpeg") != std::string::npos || 
                       clean.find(".mp3") != std::string::npos || clean.find(".zip") != std::string::npos);

    if (isSearchPrefix || isFilename) {
        Node* searchNode = m_graph.getNodeByID("FileSearchNode");
        if (searchNode) {
            LOGI(TAG, "> Route: Neural Bypass (Intent: Search) [v3.9.7-AUTO-SCROLL]");
            return searchNode;
        }
    }

    LOGI(TAG, "> Route: Chat Engine (Intent: General)");
    
    return runThompsonSampling(clean);
}

Node* GraphExecutor::runThompsonSampling(const std::string& input) {
    std::lock_guard<std::mutex> lock(m_mutex);
    LOGI(TAG, "Reasoning Spine active: [Kernel v3.9.7-AUTO-SCROLL]");

    Node* current = m_graph.getNode(1); 
    if (!current) {
        LOGE(TAG, "Thompson Sampling: Root node (ID 1) not found.");
        return nullptr;
    }

    if (current->outgoing_edges.empty()) return current;

    // ChatNode (current) is the default with weight 1.0
    uint32_t best_node_id = current->id;
    float max_sample = 1.0f;

    for (auto& edge : current->outgoing_edges) {
        float sample = m_sampler.sampleBeta(edge.success_count, edge.failure_count);
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
            std::lock_guard<std::mutex> lock(m_mutex);
            m_storage.saveGraph(m_graph);
        }
        LOGI(TAG, "GraphExecutor: Successfully synced weights to L3 Deep-store.");
        m_is_syncing.store(false);
    });
}

} // namespace Ronin::Kernel::Reasoning
