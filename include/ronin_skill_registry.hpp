#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include "capabilities/base_skill.h"

namespace Ronin::Kernel::Capability {

/**
 * Phase 4.0: Central registry for all modular skills.
 * Stabilized version: Uses non-owning pointers to prevent double-delete during transition.
 */
class SkillRegistry {
public:
    static SkillRegistry& getInstance() {
        static SkillRegistry instance;
        return instance;
    }

    void registerSkill(BaseSkill* skill) {
        m_skills[skill->getId()] = skill;
    }

    BaseSkill* getSkill(uint32_t id) {
        auto it = m_skills.find(id);
        if (it != m_skills.end()) return it->second;
        return nullptr;
    }

    const std::unordered_map<uint32_t, BaseSkill*>& getAllSkills() const {
        return m_skills;
    }

private:
    SkillRegistry() = default;
    std::unordered_map<uint32_t, BaseSkill*> m_skills;
};

} // namespace Ronin::Kernel::Capability
