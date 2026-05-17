#pragma once

#include "base_skill.h"
#include "models/inference_engine.h"

namespace Ronin::Kernel::Capability {

class ChatSkill : public BaseSkill {
public:
    ChatSkill(Ronin::Kernel::Model::InferenceEngine* engine) : m_engine(engine) {}

    std::string getName() const override { return "ChatSkill"; }
    SkillPriority getPriority() const override { return SkillPriority::CRITICAL; }
    uint32_t getLoraId() const override { return 1; }
    
    std::string execute(const std::string& param) override {
        if (m_engine) {
            return m_engine->runLiteRTReasoning(param);
        }
        return "ChatNode Error: Neural Spine not attached.";
    }

private:
    Ronin::Kernel::Model::InferenceEngine* m_engine;
};

} // namespace Ronin::Kernel::Capability
