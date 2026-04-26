#include "capabilities/file_search_node.h"
#include "ronin_log.h"
#include "intent_engine.h"
#include "memory_manager.h"
#include <algorithm>
#include <unordered_set>

#define TAG "RoninFileSearchNode"

namespace Ronin::Kernel::Capability {

FileSearchNode::FileSearchNode(Memory::LongTermMemory* ltm, NeuralEmbeddingNode* neural) 
    : m_ltm(ltm), m_neural(neural) {}

std::vector<std::string> FileSearchNode::search(const std::string& query) {
    LOGI(TAG, "Phase 5.2: Hybrid Search (Query=%s)", query.c_str());
    
    if (!m_ltm) return {"Error: Search LTM missing."};

    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    // 1. Identify Extension Priority
    std::string ext_filter = "";
    if (lower_query.find("pdf") != std::string::npos) ext_filter = ".pdf";
    else if (lower_query.find("mp3") != std::string::npos || lower_query.find("music") != std::string::npos) ext_filter = ".mp3";
    else if (lower_query.find("jpg") != std::string::npos || lower_query.find("photo") != std::string::npos) ext_filter = ".jpg";
    else if (lower_query.find("mp4") != std::string::npos || lower_query.find("video") != std::string::npos) ext_filter = ".mp4";
    else if (lower_query.find("txt") != std::string::npos || lower_query.find("doc") != std::string::npos) ext_filter = ".txt";

    std::vector<std::pair<std::string, float>> candidates;

    // 2. Step 1: Neural Match (if node is available)
    if (m_neural) {
        LOGD(TAG, "Generating BGE Query Vector...");
        auto query_vec = m_neural->generateEmbedding(query);
        auto all_embeddings = m_ltm->getAllFileEmbeddings();

        for (auto& fe : all_embeddings) {
            float sim = Ronin::Kernel::Intent::compute_cosine_similarity_neon(query_vec.data(), fe.vector.data(), 768);
            
            // Precision Boosting for specific extensions
            if (!ext_filter.empty() && fe.name.find(ext_filter) != std::string::npos) {
                sim += 0.25f; 
            }
            
            if (sim > 0.60f) {
                candidates.push_back({fe.path, sim});
            }
        }
        // Unload embedding model to save RAM as requested in Phase 5.2
        m_neural->unload();
    }

    // 3. Step 2: Keyword/Metadata Fallback (The Reliable Way)
    auto kw_results = m_ltm->searchFiles(query);
    for (const auto& path : kw_results) {
        // Boost keyword matches
        candidates.push_back({path, 0.95f});
    }

    // 4. Filter & Format
    if (candidates.empty()) return {"No matching files found in local storage."};

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::unordered_set<std::string> unique_paths;
    std::vector<std::string> output;
    output.push_back("Search Results (" + std::to_string(candidates.size()) + " items):");
    
    int count = 0;
    for (const auto& c : candidates) {
        if (unique_paths.insert(c.first).second) {
            size_t last_slash = c.first.find_last_of("/");
            std::string name = (last_slash == std::string::npos) ? c.first : c.first.substr(last_slash + 1);
            output.push_back("- " + name); 
            if (++count >= 10) break;
        }
    }

    return output;
}

std::string FileSearchNode::execute(const std::string& param) {
    auto results = search(param);
    std::string out = "";
    for (const auto& s : results) out += s + "\n";
    return out;
}

} // namespace Ronin::Kernel::Capability
