#pragma once

#include "base_skill.h"
#include "models/inference_engine.h"
#include "capabilities/hardware_bridge.h"
#include "long_term_memory.h"
#include "chat_template_formatter.h"
#include "ronin_log.h"

namespace Ronin::Kernel {
    class RoninKernel;
}

namespace Ronin::Kernel::Capability {

/**
 * Hardened v3.2: ChatSkill
 * Manages conversation flow and dynamic system prompt retrieval.
 */
class ChatSkill : public BaseSkill {
public:
    ChatSkill(Ronin::Kernel::Model::InferenceEngine* engine, Ronin::Kernel::Memory::LongTermMemory* ltm = nullptr) 
        : m_engine(engine), m_ltm(ltm) {}

    std::string getName() const override { return "ChatSkill"; }
    SkillPriority getPriority() const override { return SkillPriority::CRITICAL; }
    uint32_t getLoraId() const override { return 1; }
    
    /**
     * Executes the reasoning loop.
     * Implementation moved to .cpp to avoid incomplete type errors.
     */
    std::string execute(const std::string& param, ToolContext* context = nullptr) override;

    void setKernel(Ronin::Kernel::RoninKernel* kernel) { m_kernel = kernel; }

private:
    Ronin::Kernel::Model::InferenceEngine* m_engine;
    Ronin::Kernel::Memory::LongTermMemory* m_ltm;
    Ronin::Kernel::RoninKernel* m_kernel = nullptr;
};

} // namespace Ronin::Kernel::Capability
