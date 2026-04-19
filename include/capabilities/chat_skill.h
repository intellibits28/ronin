#pragma once

#include "base_skill.h"

namespace Ronin::Kernel::Capability {

class ChatSkill : public BaseSkill {
public:
    std::string getName() const override { return "ChatSkill"; }
    uint32_t getLoraId() const override { return 1; }
    
    std::string execute(const std::string& param) override {
        // Phase 4.0: Default chat response for the reasoning spine.
        // In a full implementation, this would call the local LLM (Gemma).
        return "ChatNode: I understand you're asking about '" + param + "'. How else can I assist you with your device or files?";
    }
};

} // namespace Ronin::Kernel::Capability
