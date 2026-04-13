#include "capabilities/file_search_node.h"
#include "ronin_log.h"

#define TAG "RoninFileSearchNode"

namespace Ronin::Kernel::Capability {

FileSearchNode::FileSearchNode(Memory::LongTermMemory& ltm) : m_ltm(ltm) {}

std::vector<std::string> FileSearchNode::execute(const std::string& query) {
    LOGI(TAG, "Executing File Search query: %s", query.c_str());
    
    // Leverage the consolidated search logic in L3 store
    auto results = m_ltm.searchFiles(query);
    
    std::vector<std::string> formatted_results;
    if (results.empty()) {
        formatted_results.push_back("No matching files found in local storage.");
    } else {
        std::string output = "Found files: \n";
        for (const auto& file : results) {
            output += "- " + file + "\n";
        }
        formatted_results.push_back(output);
    }
    
    LOGI(TAG, "Search completed. Found %zu matching files.", results.size());
    return formatted_results;
}

} // namespace Ronin::Kernel::Capability
