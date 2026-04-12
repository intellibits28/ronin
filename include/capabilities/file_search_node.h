#pragma once

#include <string>
#include <vector>
#include "long_term_memory.h"

namespace Ronin::Kernel::Capability {

class FileSearchNode {
public:
    FileSearchNode(Memory::LongTermMemory& ltm);

    /**
     * Executes the actual FTS5 search query.
     * Called when the Reasoning Spine routes to this node.
     */
    std::vector<std::string> execute(const std::string& query);

private:
    Memory::LongTermMemory& m_ltm;
};

} // namespace Ronin::Kernel::Capability
