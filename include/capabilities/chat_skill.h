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
        std::string final_prompt = param;

        // ၁။ Context Reconstruction Logic (Hardening Phase 1)
        if (m_ltm) {
            // Get last 3 pairs (6 messages) to avoid KV cache explosion
            auto raw_history = m_ltm->getHistory(6, 0);
            std::vector<Ronin::Kernel::Model::ChatMessage> history;
            
            for (const auto& p : raw_history) {
                history.push_back({p.first, p.second});
            }

            // Gemma 4 specific formatting
            final_prompt = Ronin::Kernel::Model::ChatTemplateFormatter::formatGemma4(history, param);
        }

        std::string res = "Error: Neural Spine not attached.";
        bool local_failed = true;

        if (m_engine) {
            LOGI("ChatSkill", "Reconstructed Context Length: %zu chars", final_prompt.length());
            res = m_engine->runLiteRTReasoning(final_prompt);
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

        // ၂။ Persistence (Thinking Filter logic ၎င်းထဲတွင်ပါပြီးသားဖြစ်သည်)
        if (m_ltm) {
            m_ltm->storeMessage("user", param);
            if (!local_failed) {
                m_ltm->storeMessage("assistant", res);
            }
        }

        return res;
    }

private:
    Ronin::Kernel::Model::InferenceEngine* m_engine;
    Ronin::Kernel::Memory::LongTermMemory* m_ltm;
};

} // namespace Ronin::Kernel::Capability
