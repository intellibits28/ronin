#pragma once

#include <string>
#include <vector>
#include "long_term_memory.h"
#include "neural_embedding_node.h"

namespace Ronin::Kernel::Capability {

class FileSearchNode {
public:
    FileSearchNode(Memory::LongTermMemory& ltm, NeuralEmbeddingNode* neural = nullptr);

    /**
     * Executes the actual FTS5 search query.
     * Called when the Reasoning Spine routes to this node.
     */
    std::vector<std::string> execute(const std::string& query);

private:
    Memory::LongTermMemory& m_ltm;
    NeuralEmbeddingNode* m_neural;
};

} // namespace Ronin::Kernel::Capability
