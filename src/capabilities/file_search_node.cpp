#include "capabilities/file_search_node.h"
#include "ronin_log.h"

#define TAG "RoninFileSearchNode"

namespace Ronin::Kernel::Capability {

FileSearchNode::FileSearchNode(Memory::LongTermMemory& ltm) : m_ltm(ltm) {}

std::vector<std::string> FileSearchNode::execute(const std::string& query) {
    LOGI(TAG, "Executing File Search query: %s", query.c_str());
    
    // Leverage the consolidated search logic in L3 store
    auto results = m_ltm.searchFiles(query);
    
    LOGI(TAG, "Search completed. Found %zu matching files.", results.size());
    return results;
}

} // namespace Ronin::Kernel::Capability
