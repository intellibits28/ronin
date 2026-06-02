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
    if (!m_ltm) return {"Error: Search LTM missing."};

    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    std::vector<std::string> ext_filters;
    if (lower_query.find("md") != std::string::npos) ext_filters.push_back(".md");
    if (lower_query.find("py") != std::string::npos) ext_filters.push_back(".py");

    std::vector<std::pair<std::string, float>> candidates;
    auto kw_results = m_ltm->searchFiles(query);
    for (const auto& path : kw_results) {
        float sim = 0.90f;
        for (const auto& ext : ext_filters) {
            if (path.ends_with(ext)) {
                sim = 1.0f;
                break;
            }
        }
        candidates.push_back({path, sim});
    }

    if (candidates.empty()) return {"No matching files found."};

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::vector<std::string> result_list;
    for (const auto& c : candidates) {
        result_list.push_back("- " + c.first);
    }
    return result_list;
}

std::string FileSearchNode::execute(const std::string& param, ToolContext* context) {
    (void)context;
    m_last_results = search(param);
    if (m_last_results.empty()) return "No files found.";
    std::string out = "Results:\n";
    for (const auto& r : m_last_results) out += r + "\n";
    return out;
}

} // namespace Ronin::Kernel::Capability
