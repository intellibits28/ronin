#pragma once

#include "base_skill.h"
#include "models/inference_engine.h"
#include "capabilities/hardware_bridge.h"
#include "ronin_log.h"

namespace Ronin::Kernel::Capability {

class ChatSkill : public BaseSkill {
public:
    ChatSkill(Ronin::Kernel::Model::InferenceEngine* engine) : m_engine(engine) {}

    std::string getName() const override { return "ChatSkill"; }
    SkillPriority getPriority() const override { return SkillPriority::CRITICAL; }
    uint32_t getLoraId() const override { return 1; }
    
    std::string execute(const std::string& param) override {
        std::string res = "Error: Neural Spine not attached.";
        bool local_failed = true;

        if (m_engine) {
            res = m_engine->runLiteRTReasoning(param);
            if (!res.empty() && !res.starts_with("Error:")) {
                local_failed = false;
            }
        }

        // Phase 11.0: Agentic Cloud Fallback
        if (local_failed) {
            LOGW("ChatSkill", "Local reasoning unavailable or failed. Escalating to Cloud...");
            std::string provider = "Gemini"; 
            std::string apiKey = HardwareBridge::getCloudApiKey(provider);
            
            if (!apiKey.empty()) {
                std::string cloudRes = HardwareBridge::fetchCloudResponse(param, provider, apiKey);
                if (!cloudRes.empty() && !cloudRes.starts_with("Error:")) {
                    return cloudRes;
                }
                return res + " (Cloud Fallback also failed: " + cloudRes + ")";
            }
        }

        return res;
    }

private:
    Ronin::Kernel::Model::InferenceEngine* m_engine;
};

} // namespace Ronin::Kernel::Capability
