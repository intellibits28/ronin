#include "capabilities/file_search_node.h"
#include "ronin_log.h"
#include "intent_engine.h"
#include "memory_manager.h"
#include <algorithm>
#include <unordered_set>

#define TAG "RoninFileSearchNode"

namespace Ronin::Kernel::Capability {

FileSearchNode::FileSearchNode(Memory::LongTermMemory* ltm) 
    : m_ltm(ltm) {}

std::vector<std::string> FileSearchNode::search(const std::string& query) {
    LOGI(TAG, "Phase 6.2: FTS5 File Search (Query=%s)", query.c_str());
    
    if (!m_ltm) return {"Error: Search LTM missing."};

    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    // 1. Identify Extension Priority
    std::vector<std::string> ext_filters;
    
    // Developer & Text
    if (lower_query.find("md") != std::string::npos || lower_query.find("markdown") != std::string::npos) ext_filters.push_back(".md");
    if (lower_query.find("py") != std::string::npos || lower_query.find("python") != std::string::npos) ext_filters.push_back(".py");
    if (lower_query.find("zig") != std::string::npos) ext_filters.push_back(".zig");
    if (lower_query.find("cpp") != std::string::npos || lower_query.find("c++") != std::string::npos) ext_filters.insert(ext_filters.end(), {".cpp", ".h", ".hpp"});
    if (lower_query.find("json") != std::string::npos) ext_filters.push_back(".json");
    if (lower_query.find("yml") != std::string::npos || lower_query.find("yaml") != std::string::npos) ext_filters.insert(ext_filters.end(), {".yml", ".yaml"});
    
    // Media & Docs
    if (lower_query.find("pdf") != std::string::npos) ext_filters.push_back(".pdf");
    if (lower_query.find("txt") != std::string::npos || lower_query.find("text") != std::string::npos) ext_filters.push_back(".txt");
    
    if (lower_query.find("mp3") != std::string::npos || 
        lower_query.find("music") != std::string::npos ||
        lower_query.find("audio") != std::string::npos ||
        lower_query.find("song") != std::string::npos ||
        lower_query.find("သီချင်း") != std::string::npos) {
        ext_filters.insert(ext_filters.end(), {".mp3", ".m4a", ".wav", ".ogg", ".flac"});
    }
    
    if (lower_query.find("mp4") != std::string::npos || 
        lower_query.find("video") != std::string::npos ||
        lower_query.find("movie") != std::string::npos ||
        lower_query.find("ဗီဒီယို") != std::string::npos) {
        ext_filters.insert(ext_filters.end(), {".mp4", ".mkv", ".avi", ".mov", ".webm"});
    }

    if (lower_query.find("doc") != std::string::npos || 
        lower_query.find("document") != std::string::npos ||
        lower_query.find("စာရွက်စာတမ်း") != std::string::npos) {
        ext_filters.insert(ext_filters.end(), {".docx", ".doc", ".pdf", ".txt", ".odt"});
    }

    std::vector<std::pair<std::string, float>> candidates;

    // 2. Keyword Search (Direct SQLite LIKE / FTS5)
    auto kw_results = m_ltm->searchFiles(query);
    LOGI(TAG, "Keyword Search Results: %zu found", kw_results.size());
    for (const auto& path : kw_results) {
        float sim = 0.90f;
        for (const auto& ext : ext_filters) {
            if (path.length() >= ext.length() && 
                path.compare(path.length() - ext.length(), ext.length(), ext) == 0) {
                sim = 1.0f; // Perfect match for keyword + extension
                break;
            }
        }
        candidates.push_back({path, sim});
    }

    // 3. Format Output List
    if (candidates.empty()) return {"No matching files found in local storage."};

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::unordered_set<std::string> unique_paths;
    std::vector<std::string> result_list;
    for (const auto& c : candidates) {
        if (unique_paths.insert(c.first).second) {
            size_t last_slash = c.first.find_last_of("/");
            std::string name = (last_slash == std::string::npos) ? c.first : c.first.substr(last_slash + 1);
            result_list.push_back("- " + name + " [" + c.first + "]"); 
        }
    }

    return result_list;
}

std::string FileSearchNode::execute(const std::string& param) {
    std::string lower_param = param;
    std::transform(lower_param.begin(), lower_param.end(), lower_param.begin(), ::tolower);

    // Phase 4.5.9: Handle explicit /search and /find prefixes
    std::string clean_query = param;
    if (lower_param.starts_with("/search ")) clean_query = param.substr(8);
    else if (lower_param.starts_with("/find ")) clean_query = param.substr(6);
    else if (lower_param == "/search" || lower_param == "/find") return "Usage: /search <filename>";

    // Phase 6.2: Pagination Logic (/more or arrow-like triggers)
    if (lower_param.find("/more") != std::string::npos || lower_param.find("next") != std::string::npos || lower_param == ">") {
        if (m_last_results.empty()) return "No previous search results to show.";
        
        m_last_offset += 5;
        if (m_last_offset >= m_last_results.size()) {
            m_last_offset = 0; // Reset or keep end? Let's reset for cycle.
            return "End of list. Type search again for fresh results.";
        }
    } else {
        // Fresh search
        m_last_results = search(clean_query);
        m_last_offset = 0;
    }

    if (m_last_results.empty()) return "No matching files found.";

    std::string out = "Search Results (showing " + std::to_string(std::min((size_t)5, m_last_results.size() - m_last_offset)) + 
                      " of " + std::to_string(m_last_results.size()) + "):\n";
    
    size_t count = 0;
    for (size_t i = m_last_offset; i < m_last_results.size() && count < 5; ++i, ++count) {
        out += m_last_results[i] + "\n";
    }

    if (m_last_offset + 5 < m_last_results.size()) {
        out += "... and " + std::to_string(m_last_results.size() - m_last_offset - 5) + " more. [Type /more to see next]\n";
    }

    return out;
}

} // namespace Ronin::Kernel::Capability
