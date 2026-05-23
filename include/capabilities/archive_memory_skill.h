#pragma once

#include "base_skill.h"
#include "memory_database.h"

namespace Ronin::Kernel::Capability {

class ArchiveMemorySkill : public BaseSkill {
public:
    ArchiveMemorySkill(Ronin::Kernel::Data::MemoryDatabase* db) : m_db(db) {}

    std::string getName() const override { return "ArchiveMemorySkill"; }
    SkillPriority getPriority() const override { return SkillPriority::MEDIUM; }
    uint32_t getLoraId() const override { return 3; }

    std::string execute(const std::string& param) override {
        if (!m_db) return "ArchiveNode Error: Database not attached.";

        // In a production scenario, we'd run a segmenter here. 
        // For Phase 4, we use the raw text as segmented text for mock testing.
        bool ok = m_db->insertMemory(param, param, Ronin::Kernel::Data::MemoryState::ACTIVE, "ai_archive");
        
        return ok ? "Successfully archived to long-term memory." : "Error: Archive failed.";
    }

private:
    Ronin::Kernel::Data::MemoryDatabase* m_db;
};

} // namespace Ronin::Kernel::Capability
