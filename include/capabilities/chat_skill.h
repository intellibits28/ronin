#pragma once

#include "base_skill.h"
#include "models/inference_engine.h"
#include "capabilities/hardware_bridge.h"
#include "long_term_memory.h"
#include "chat_template_formatter.h"
#include "ronin_log.h"

namespace Ronin::Kernel::Capability {

class ChatSkill : public BaseSkill {
public:
    ChatSkill(Ronin::Kernel::Model::InferenceEngine* engine, Ronin::Kernel::Memory::LongTermMemory* ltm = nullptr) 
        : m_engine(engine), m_ltm(ltm) {}

    std::string getName() const override { return "ChatSkill"; }
    SkillPriority getPriority() const override { return SkillPriority::CRITICAL; }
    uint32_t getLoraId() const override { return 1; }
    
    std::string execute(const std::string& param) override {
        // ၁။ User Message ကို Database ထဲ အရင်သိမ်းမည်
        if (m_ltm) {
            m_ltm->storeMessage("user", param);
        }

        std::string res = "Error: Neural Spine not attached.";
        bool local_failed = true;

        if (m_engine) {
            // Retrieve dynamic system prompt from kernel if available
            std::string sysPrompt = "";
            if (m_kernel) {
                sysPrompt = m_kernel->getSuggestedSubject();
            }

            // Phase 11.2: Direct prompt passing with custom instructions.
            res = m_engine->runLiteRTReasoning(param, sysPrompt);
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
                    res = cloudRes;
                    local_failed = false;
                } else {
                    return res + " (Cloud Fallback also failed: " + cloudRes + ")";
                }
            }
        }

        // ၂။ Persistence Assistant Response
        if (m_ltm && !local_failed) {
            m_ltm->storeMessage("assistant", res);
        }

        return res;
    }

    void setKernel(RoninKernel* kernel) { m_kernel = kernel; }

private:
    Ronin::Kernel::Model::InferenceEngine* m_engine;
    Ronin::Kernel::Memory::LongTermMemory* m_ltm;
    RoninKernel* m_kernel = nullptr;
};

} // namespace Ronin::Kernel::Capability
