#pragma once

#include "base_skill.h"
#include "long_term_memory.h"
#include <sstream>

namespace Ronin::Kernel::Capability {

class MemorySearchSkill : public BaseSkill {
public:
    MemorySearchSkill(Ronin::Kernel::Memory::LongTermMemory* ltm) : m_ltm(ltm) {}

    std::string getName() const override { return "MemorySearchSkill"; }
    SkillPriority getPriority() const override { return SkillPriority::MEDIUM; }
    uint32_t getLoraId() const override { return 2; }

    std::string execute(const std::string& param, ToolContext* context = nullptr) override {
        if (!m_ltm) return "MemoryNode Error: Memory Spine not attached.";

        auto results = m_ltm->search(param);

        if (results.empty()) {
            return "No relevant memories found for: " + param;
        }

        std::stringstream ss;
        ss << "Cognitive Recall [Top Chunks]:\n";
        for (const auto& entry : results) {
            ss << "- " << entry << "\n";
        }
        
        return ss.str();
    }

private:
    Ronin::Kernel::Memory::LongTermMemory* m_ltm;
};

} // namespace Ronin::Kernel::Capability
