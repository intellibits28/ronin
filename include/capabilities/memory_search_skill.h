#pragma once

#include "base_skill.h"
#include "memory_database.h"
#include <sstream>

namespace Ronin::Kernel::Capability {

class MemorySearchSkill : public BaseSkill {
public:
    MemorySearchSkill(Ronin::Kernel::Data::MemoryDatabase* db) : m_db(db) {}

    std::string getName() const override { return "MemorySearchSkill"; }
    SkillPriority getPriority() const override { return SkillPriority::MEDIUM; }
    uint32_t getLoraId() const override { return 2; } // Dedicated LoRA for search if needed

    std::string execute(const std::string& param) override {
        if (!m_db) return "MemoryNode Error: Database not attached.";

        // Perform FTS5 search (BM25)
        auto results = m_db->searchFTS(param, 3); // Top-K = 3 Chunks as per Spec v2.1

        if (results.empty()) {
            return "No relevant memories found for: " + param;
        }

        std::stringstream ss;
        ss << "Cognitive Recall [Top-3 Chunks]:\n";
        for (const auto& entry : results) {
            ss << "- " << entry.raw_text_mm << " (State: " << stateToString(entry.state) << ")\n";
        }
        
        return ss.str();
    }

private:
    Ronin::Kernel::Data::MemoryDatabase* m_db;

    std::string stateToString(Ronin::Kernel::Data::MemoryState state) {
        switch (state) {
            case Ronin::Kernel::Data::MemoryState::ACTIVE: return "Active";
            case Ronin::Kernel::Data::MemoryState::COLD: return "Cold";
            case Ronin::Kernel::Data::MemoryState::ARCHIVED: return "Archived";
            case Ronin::Kernel::Data::MemoryState::FORGOTTEN: return "Forgotten";
            case Ronin::Kernel::Data::MemoryState::TOMBSTONED: return "Tombstoned";
            default: return "Unknown";
        }
    }
};

} // namespace Ronin::Kernel::Capability
