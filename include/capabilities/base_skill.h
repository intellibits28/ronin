#pragma once

#include <string>
#include <memory>

namespace Ronin::Kernel::Capability {

/**
 * Phase 4.0: Survival Triage
 * Defines the priority of a skill for resource allocation.
 */
enum class SkillPriority {
    CRITICAL = 0,   // Instant execution required (e.g. Chat)
    HIGH = 1,       // Hardware control (e.g. Flashlight)
    MEDIUM = 2,     // Info retrieval (e.g. GPS)
    LOW = 3         // Background tasks (e.g. File Indexing)
};

/**
 * Phase 4.0: Vtable-based Registry Foundation.
 * This interface decouples intent resolution from physical execution.
 */
class BaseSkill {
public:
    virtual ~BaseSkill() = default;

    /**
     * Primary execution entry point for the skill.
     * @param param The extracted parameter for this tool.
     * @return A response string for the Reasoning Console/UI.
     */
    virtual std::string execute(const std::string& param) = 0;

    /**
     * @return The internal registration name of this skill.
     */
    virtual std::string getName() const = 0;

    /**
     * @return The resource priority of this skill.
     */
    virtual SkillPriority getPriority() const { return SkillPriority::MEDIUM; }

    /**
     * Phase 4.0: LoRA State Diff Integration.
     * @return The unique ID of the LoRA adapter required for this skill.
     * Defaults to 0 (No specific LoRA required).
     */
    virtual uint32_t getLoraId() const { return 0; }
};

} // namespace Ronin::Kernel::Capability
