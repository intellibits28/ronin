#pragma once

#include <string>
#include <vector>
#include "long_term_memory.h"
#include "neural_embedding_node.h"
#include "base_skill.h"

namespace Ronin::Kernel::Capability {

class FileSearchNode : public BaseSkill {
public:
    FileSearchNode() : m_ltm(nullptr), m_neural(nullptr) {}
    FileSearchNode(Memory::LongTermMemory* ltm, NeuralEmbeddingNode* neural = nullptr);

    // BaseSkill Implementation
    std::string getName() const override { return "FileSearchNode"; }
    SkillPriority getPriority() const override { return SkillPriority::LOW; }
    uint32_t getLoraId() const override { return 2; }
    std::string execute(const std::string& param) override;

    /**
     * Executes the actual FTS5 search query.
     */
    std::vector<std::string> search(const std::string& query);

private:
    Memory::LongTermMemory* m_ltm;
    NeuralEmbeddingNode* m_neural;
    
    // Phase 6.2: Pagination State
    std::vector<std::string> m_last_results;
    size_t m_last_offset = 0;
};

} // namespace Ronin::Kernel::Capability
