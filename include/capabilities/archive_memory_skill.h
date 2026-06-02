#pragma once

#include "base_skill.h"
#include "long_term_memory.h"

namespace Ronin::Kernel::Capability {

class ArchiveMemorySkill : public BaseSkill {
public:
    ArchiveMemorySkill(Ronin::Kernel::Memory::LongTermMemory* ltm) : m_ltm(ltm) {}

    std::string getName() const override { return "ArchiveMemorySkill"; }
    SkillPriority getPriority() const override { return SkillPriority::MEDIUM; }
    uint32_t getLoraId() const override { return 3; }

    std::string execute(const std::string& param, ToolContext* context = nullptr) override {
        if (!m_ltm) return "ArchiveNode Error: Memory Spine not attached.";

        bool ok = m_ltm->consolidate(param);
        
        return ok ? "Successfully archived to long-term memory." : "Error: Archive failed.";
    }

private:
    Ronin::Kernel::Memory::LongTermMemory* m_ltm;
};

} // namespace Ronin::Kernel::Capability
