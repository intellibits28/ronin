#include "capabilities/file_search_node.h"
#include "ronin_log.h"
#include "intent_engine.h"
#include <algorithm>

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

    // 2. Try Neural Vector Search first
    if (m_neural && m_neural->isLoaded()) {
        LOGI(TAG, "> Search Mode: Neural");
        auto query_vec = m_neural->execute(query);
        auto all_embeddings = m_ltm.getAllFileEmbeddings();
        
        std::vector<std::string> neural_results;
        for (auto& fe : all_embeddings) {
            float sim = Ronin::Kernel::Intent::compute_cosine_similarity_neon(query_vec.data(), fe.vector.data(), 384);
            if (sim > 0.7f) {
                neural_results.push_back(fe.name);
            }
        }

        if (!neural_results.empty()) {
            std::string output = "Found files (Neural): \n";
            for (const auto& file : neural_results) {
                output += "- " + file + "\n";
            }
            return {output};
        }
    }

    // 3. Fallback to Keyword (FTS5) Search
    LOGI(TAG, "> Search Mode: Keyword Fallback");
    auto results = m_ltm.searchFiles(query);
    
    std::vector<std::string> formatted_results;
    if (results.empty()) {
        formatted_results.push_back("No matching files found in local storage.");
    } else {
        std::string output = "Found files (Keyword): \n";
        for (const auto& file : results) {
            output += "- " + file + "\n";
        }
        formatted_results.push_back(output);
    }
    
    return formatted_results;
}

} // namespace Ronin::Kernel::Capability
