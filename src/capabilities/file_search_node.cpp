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
    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    if (lower_query.find("pdf") != std::string::npos || lower_query.find("document") != std::string::npos || lower_query.find("docx") != std::string::npos) {
        if (lower_query.find("docx") != std::string::npos) type_hint = ".docx";
        else type_hint = ".pdf";
    }
    else if (lower_query.find("jpg") != std::string::npos || lower_query.find("jpeg") != std::string::npos || lower_query.find("image") != std::string::npos || lower_query.find("photo") != std::string::npos) type_hint = ".jpg";
    else if (lower_query.find("mp3") != std::string::npos || lower_query.find("music") != std::string::npos || lower_query.find("audio") != std::string::npos || lower_query.find("song") != std::string::npos) type_hint = ".mp3";
    else if (lower_query.find("video") != std::string::npos || lower_query.find("movie") != std::string::npos || lower_query.find("mp4") != std::string::npos || lower_query.find("mkv") != std::string::npos) {
        if (lower_query.find("mkv") != std::string::npos) type_hint = ".mkv";
        else type_hint = ".mp4";
    }
    else if (lower_query.find("zip") != std::string::npos || lower_query.find("archive") != std::string::npos) type_hint = ".zip";
    else if (lower_query.find("txt") != std::string::npos || lower_query.find("note") != std::string::npos || lower_query.find("text") != std::string::npos || lower_query.find("document") != std::string::npos) type_hint = ".txt";

    if (!type_hint.empty()) {
        LOGI(TAG, "> Active Search Filter: [Extension=%s]", type_hint.c_str());
    } else {
        LOGW(TAG, "> No valid category identified. Aborting search to prevent data leakage.");
        return {"Error: Invalid search category. Please specify a file type (e.g., 'search music')."};
    }

    // 3. Try Neural Vector Search first
    if (m_neural && m_neural->isLoaded()) {
        LOGI(TAG, "> Search Mode: Neural");
        auto query_vec = m_neural->execute(query);
        auto all_embeddings = m_ltm.getAllFileEmbeddings();
        
        std::vector<std::pair<std::string, float>> neural_matches;
        for (auto& fe : all_embeddings) {
            // EXPLICIT PRIVACY GUARD: Never leak system files
            std::string filename = fe.name;
            std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
            if (filename.find(".env") != std::string::npos || filename.find(".ignore") != std::string::npos || filename.find("config") != std::string::npos) {
                continue;
            }

            // EXPLICIT FILTER: If we have a type hint, skip files that don't match it
            if (!type_hint.empty()) {
                std::string filename = fe.name;
                std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
                // Strict extension check (ends_with)
                if (filename.length() < type_hint.length() || 
                    filename.compare(filename.length() - type_hint.length(), type_hint.length(), type_hint) != 0) {
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
            size_t display_count = std::min(unique_names.size(), size_t(5));
            for (size_t i = 0; i < display_count; ++i) {
                output += "- " + unique_names[i] + "\n";
            }
            if (unique_names.size() > 5) {
                output += "... and " + std::to_string(unique_names.size() - 5) + " more files found.\n";
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
            
            // EXPLICIT PRIVACY GUARD: Never leak system files
            if (filename.find(".env") != std::string::npos || filename.find(".ignore") != std::string::npos || filename.find("config") != std::string::npos) {
                continue;
            }

            // Strict extension check (ends_with)
            if (filename.length() >= type_hint.length() && 
                filename.compare(filename.length() - type_hint.length(), type_hint.length(), type_hint) == 0) {
                filtered_results.push_back(file);
            }
        }
    } else {
        // This branch should ideally not be reached due to the guard at step 2.
        return {"No matching files found in local storage."};
    }

    auto unique_results = Memory::MemoryManager::filterDuplicateFilenames(filtered_results);
    
    std::vector<std::string> formatted_results;
    if (unique_results.empty()) {
        formatted_results.push_back("No matching files found in local storage.");
    } else {
        std::string output = "Found files (Keyword): \n";
        size_t display_count = std::min(unique_results.size(), size_t(5));
        for (size_t i = 0; i < display_count; ++i) {
            output += "- " + unique_results[i] + "\n";
        }
        if (unique_results.size() > 5) {
            output += "... and " + std::to_string(unique_results.size() - 5) + " more files found.\n";
        }
        formatted_results.push_back(output);
    }
    
    return formatted_results;
}

} // namespace Ronin::Kernel::Capability
