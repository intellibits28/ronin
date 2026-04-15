#include "capabilities/file_search_node.h"
#include "ronin_log.h"
#include "intent_engine.h"
#include "memory_manager.h"
#include <algorithm>
#include <unordered_set>

#define TAG "RoninFileSearchNode"

namespace Ronin::Kernel::Capability {

FileSearchNode::FileSearchNode(Memory::LongTermMemory& ltm, NeuralEmbeddingNode* neural) 
    : m_ltm(ltm), m_neural(neural) {}

std::vector<std::string> FileSearchNode::execute(const std::string& query) {
    LOGI(TAG, "Executing File Search query: %s", query.c_str());
    
    // 1. Check Model Health
    if (m_neural && !m_neural->isLoaded()) {
        LOGE(TAG, "> FATAL: ONNX Runtime failed to load model weights!");
    }

    // 2. Identify Type Hints (Extensions)
    std::string type_hint = "";
    if (query.find("pdf") != std::string::npos) type_hint = ".pdf";
    else if (query.find("jpg") != std::string::npos || query.find("jpeg") != std::string::npos) type_hint = ".jpg";
    else if (query.find("mp3") != std::string::npos) type_hint = ".mp3";
    else if (query.find("zip") != std::string::npos) type_hint = ".zip";
    else if (query.find("txt") != std::string::npos) type_hint = ".txt";

    if (!type_hint.empty()) {
        LOGI(TAG, "> Active Search Filter: [Extension=%s]", type_hint.c_str());
    }

    // 3. Try Neural Vector Search first
    if (m_neural && m_neural->isLoaded()) {
        LOGI(TAG, "> Search Mode: Neural");
        auto query_vec = m_neural->execute(query);
        auto all_embeddings = m_ltm.getAllFileEmbeddings();
        
        std::vector<std::pair<std::string, float>> neural_matches;
        for (auto& fe : all_embeddings) {
            // EXPLICIT FILTER: If we have a type hint, skip files that don't match it
            if (!type_hint.empty()) {
                std::string filename = fe.name;
                std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
                if (filename.find(type_hint) == std::string::npos) {
                    continue;
                }
            }

            float sim = Ronin::Kernel::Intent::compute_cosine_similarity_neon(query_vec.data(), fe.vector.data(), 384);
            if (sim > 0.7f) {
                neural_matches.push_back({fe.name, sim});
            }
        }

        if (!neural_matches.empty()) {
            std::sort(neural_matches.begin(), neural_matches.end(), [](const auto& a, const auto& b) {
                return a.second > b.second;
            });

            // Extract names and deduplicate
            std::vector<std::string> names;
            for (const auto& nm : neural_matches) names.push_back(nm.first);
            auto unique_names = Memory::MemoryManager::filterDuplicateFilenames(names);

            std::string output = "Found files (Neural): \n";
            for (size_t i = 0; i < std::min(unique_names.size(), size_t(5)); ++i) {
                output += "- " + unique_names[i] + "\n";
            }
            return {output};
        }
    }

    // 4. Fallback to Keyword (FTS5) Search
    LOGI(TAG, "> Search Mode: Keyword Fallback");
    auto results = m_ltm.searchFiles(query);
    
    // EXPLICIT FILTER: Apply extension filter to keyword results
    std::vector<std::string> filtered_results;
    if (!type_hint.empty()) {
        for (const auto& file : results) {
            std::string filename = file;
            std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
            if (filename.find(type_hint) != std::string::npos) {
                filtered_results.push_back(file);
            }
        }
    } else {
        filtered_results = results;
    }

    auto unique_results = Memory::MemoryManager::filterDuplicateFilenames(filtered_results);
    
    std::vector<std::string> formatted_results;
    if (unique_results.empty()) {
        formatted_results.push_back("No matching files found in local storage.");
    } else {
        std::string output = "Found files (Keyword): \n";
        for (const auto& file : unique_results) {
            output += "- " + file + "\n";
        }
        formatted_results.push_back(output);
    }
    
    return formatted_results;
}

} // namespace Ronin::Kernel::Capability
