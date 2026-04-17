#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "../ronin_types.hpp"

namespace Ronin::Kernel::Capability {

/**
 * Phase 4.0: Base interface for all kernel skills.
 * Implements a Vtable-based registry pattern to decouple intents from execution.
 */
class BaseSkill {
public:
    virtual ~BaseSkill() = default;

    /**
     * Unique identifier for the skill (maps to Node ID).
     */
    virtual uint32_t getId() const = 0;

    /**
     * Human-readable name of the skill.
     */
    virtual std::string getName() const = 0;

    /**
     * Primary execution entry point.
     */
    virtual Result execute(const CognitiveIntent& intent) = 0;

    /**
     * Capability Manifest data for the IntentEngine.
     */
    virtual std::vector<std::string> getSubjects() const = 0;
    virtual std::vector<std::string> getActions() const = 0;
};

} // namespace Ronin::Kernel::Capability
