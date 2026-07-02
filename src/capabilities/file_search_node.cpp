#include "capabilities/file_search_node.h"
#include "capabilities/hardware_bridge.h"
#include "ronin_log.h"
#include "intent_engine.h"
#include "memory_manager.h"
#include <algorithm>
#include <unordered_set>
#include <nlohmann/json.hpp>

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
    if (lower_query.find("pdf") != std::string::npos) ext_filters.push_back(".pdf");
    if (lower_query.find("txt") != std::string::npos) ext_filters.push_back(".txt");

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

    if (candidates.empty()) return {};

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::vector<std::string> result_list;
    for (const auto& c : candidates) {
        result_list.push_back(c.first);
    }
    return result_list;
}

std::string FileSearchNode::execute(const std::string& param, ToolContext* context) {
    (void)context;

    // Extract query from JSON payload if possible
    std::string query = param;
    try {
        auto j = nlohmann::json::parse(param);
        if (j.contains("query")) query = j["query"].get<std::string>();
        else if (j.contains("original_query")) query = j["original_query"].get<std::string>();
    } catch (...) {}

    LOGI(TAG, "FileSearchNode::execute query='%s'", query.c_str());
    m_last_results = search(query);

    if (m_last_results.empty()) {
        HardwareBridge::pushMessage("[FILES FOUND] No files found matching: '" + query + "'.");
        return "No files found.";
    }

    std::string out = "Results:\n";
    std::string push_msg = "[FILES FOUND]\n";
    for (const auto& r : m_last_results) {
        out += "- " + r + "\n";
        push_msg += r + "\n";
    }
    LOGI(TAG, "FileSearchNode::execute found %zu results", m_last_results.size());
    HardwareBridge::pushMessage(push_msg);
    return out;
}

} // namespace Ronin::Kernel::Capability
