#pragma once

#include <string>
#include <vector>
#include "long_term_memory.h"
#include "neural_embedding_node.h"
#include "base_skill.h"

namespace Ronin::Kernel::Capability {

class FileSearchNode : public BaseSkill {
public:
    FileSearchNode(Memory::LongTermMemory& ltm, NeuralEmbeddingNode* neural = nullptr);

    // BaseSkill Implementation
    uint32_t getId() const override { return 2; }
    std::string getName() const override { return "FileSearchNode"; }
    Result execute(const CognitiveIntent& intent) override;
    
    std::vector<std::string> getSubjects() const override {
        return {"file", "document", "pdf", "image", "music", "mp3", "video", "text"};
    }
    std::vector<std::string> getActions() const override {
        return {"search", "find", "locate", "get"};
    }

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
