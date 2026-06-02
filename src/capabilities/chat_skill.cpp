#include "capabilities/chat_skill.h"
#include "ronin_kernel.hpp"
#include "ronin_log.h"

namespace Ronin::Kernel::Capability {

std::string ChatSkill::execute(const std::string& param, ToolContext* context) {
    (void)context;
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

} // namespace Ronin::Kernel::Capability
