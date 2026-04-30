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
    LOGI(TAG, "Phase 5.9: Broadened Search (Query=%s)", query.c_str());
    
    if (!m_ltm) return {"Error: Search LTM missing."};

    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    // 1. Identify Extension Priority (Broadened for Phase 5.9)
    std::vector<std::string> ext_filters;
    if (lower_query.find("pdf") != std::string::npos) ext_filters.push_back(".pdf");
    
    if (lower_query.find("mp3") != std::string::npos || 
        lower_query.find("music") != std::string::npos ||
        lower_query.find("audio") != std::string::npos ||
        lower_query.find("song") != std::string::npos) {
        ext_filters.insert(ext_filters.end(), {".mp3", ".m4a", ".wav", ".ogg"});
    }
    
    if (lower_query.find("jpg") != std::string::npos || 
        lower_query.find("photo") != std::string::npos ||
        lower_query.find("image") != std::string::npos) {
        ext_filters.insert(ext_filters.end(), {".jpg", ".jpeg", ".png", ".webp"});
    }

    if (lower_query.find("mp4") != std::string::npos || 
        lower_query.find("video") != std::string::npos ||
        lower_query.find("movie") != std::string::npos) {
        ext_filters.insert(ext_filters.end(), {".mp4", ".mkv", ".avi", ".mov"});
    }

    if (lower_query.find("txt") != std::string::npos || 
        lower_query.find("doc") != std::string::npos ||
        lower_query.find("text") != std::string::npos ||
        lower_query.find("code") != std::string::npos ||
        lower_query.find("script") != std::string::npos) {
        ext_filters.insert(ext_filters.end(), {".txt", ".pdf", ".docx", ".fbs", ".md", ".json", ".yml", ".yaml", ".zig", ".py", ".cpp", ".h"});
    }

    std::vector<std::pair<std::string, float>> candidates;

    // 2. Step 1: Neural Match
    if (m_neural) {
        LOGD(TAG, "Generating BGE Query Vector...");
        auto query_vec = m_neural->generateEmbedding(query);
        auto all_embeddings = m_ltm->getAllFileEmbeddings();

        for (auto& fe : all_embeddings) {
            // Exclude common noise
            if (fe.name.find(".database_uuid") != std::string::npos || 
                fe.name.find(".nomedia") != std::string::npos ||
                fe.name.find(".db") != std::string::npos) continue;

            float sim = Ronin::Kernel::Intent::compute_cosine_similarity_neon(query_vec.data(), fe.vector.data(), 768);
            
            // Priority Boosting for matched extensions
            bool ext_match = false;
            for (const auto& ext : ext_filters) {
                if (fe.name.length() >= ext.length() && 
                    fe.name.compare(fe.name.length() - ext.length(), ext.length(), ext) == 0) {
                    sim += 0.10f; // Lowered from 0.20 to prevent over-shadowing neural relevance
                    ext_match = true;
                    break;
                }
            }
            
            // Phase 5.11: Soft Fallback & High Recall
            if (ext_match || sim > 0.80f || ext_filters.empty()) {
                if (sim > 0.65f) { // Final recall threshold
                    candidates.push_back({fe.path, sim});
                }
            }
        }
        m_neural->unload();
    }

    // 3. Step 2: Keyword Fallback
    auto kw_results = m_ltm->searchFiles(query);
    for (const auto& path : kw_results) {
        candidates.push_back({path, 0.95f});
    }

    // 4. Format Output
    if (candidates.empty()) return {"No matching files found in local storage."};

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::unordered_set<std::string> unique_paths;
    std::vector<std::string> output;
    output.push_back("Search Results (" + std::to_string(std::min(candidates.size(), (size_t)10)) + " items):");
    
    int count = 0;
    for (const auto& c : candidates) {
        if (unique_paths.insert(c.first).second) {
            size_t last_slash = c.first.find_last_of("/");
            std::string name = (last_slash == std::string::npos) ? c.first : c.first.substr(last_slash + 1);
            // Phase 6.0: Developer-Level Transparency (Include Path)
            output.push_back("- " + name + " [" + c.first + "]"); 
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
