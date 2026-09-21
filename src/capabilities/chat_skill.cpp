#include "capabilities/chat_skill.h"
#include "ronin_kernel.hpp"
#include "ronin_log.h"

#include <unordered_set>
#include <algorithm>

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

        // v2.0: Inject dynamic environment sensor fusion state from LTM
        std::string latest_state = "unknown";
        if (m_ltm) {
            latest_state = m_ltm->getLatestPerceptionState();
        }
        sysPrompt += "\n[SYSTEM ENVIRONMENT CONTEXT]:\n";
        sysPrompt += "Current classified physical activity state: " + latest_state + "\n";
        if (latest_state == "walking" || latest_state == "running" || latest_state == "active") {
            sysPrompt += "This state implies physical movement and presence of a person in the immediate vicinity. Respond based on this sensor context if asked about people presence.\n";
        } else if (latest_state == "phone_on_table") {
            sysPrompt += "This state implies the device is resting quietly on a surface (possibly unoccupied room).\n";
        }

        // v2.0: Inject relevant database documentation notes/facts for user queries
        if (m_ltm) {
            auto matched = m_ltm->searchNotes(param);
            
            // Check for help/capability queries and auto-load seeded guidelines
            std::string lower_query = param;
            std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
            if (lower_query.find("capability") != std::string::npos ||
                lower_query.find("capabilities") != std::string::npos ||
                lower_query.find("ability") != std::string::npos ||
                lower_query.find("abilities") != std::string::npos ||
                lower_query.find("feature") != std::string::npos ||
                lower_query.find("features") != std::string::npos ||
                lower_query.find("tool") != std::string::npos ||
                lower_query.find("tools") != std::string::npos ||
                lower_query.find("skill") != std::string::npos ||
                lower_query.find("skills") != std::string::npos ||
                lower_query.find("what can you do") != std::string::npos ||
                lower_query.find("who are you") != std::string::npos ||
                lower_query.find("how to use") != std::string::npos ||
                lower_query.find("about") != std::string::npos ||
                lower_query.find("ronin") != std::string::npos ||
                lower_query.find("help") != std::string::npos ||
                lower_query.find("စွမ်းရည်") != std::string::npos ||
                lower_query.find("လုပ်နိုင်") != std::string::npos ||
                lower_query.find("ဘာလုပ်") != std::string::npos ||
                lower_query.find("ဘာတွေလုပ်") != std::string::npos ||
                lower_query.find("လုပ်ဆောင်နိုင်") != std::string::npos ||
                lower_query.find("ဘယ်လိုသုံး") != std::string::npos ||
                lower_query.find("အကြောင်း") != std::string::npos ||
                lower_query.find("မင်းက ဘာလဲ") != std::string::npos ||
                lower_query.find("မင်းဘယ်သူ") != std::string::npos ||
                lower_query.find("ကူညီ") != std::string::npos ||
                lower_query.find("dsp") != std::string::npos ||
                lower_query.find("sensor") != std::string::npos ||
                lower_query.find("shm") != std::string::npos ||
                lower_query.find("guitar") != std::string::npos ||
                lower_query.find("tuner") != std::string::npos ||
                lower_query.find("pitch") != std::string::npos ||
                lower_query.find("ဂစ်တာ") != std::string::npos ||
                lower_query.find("ကြိုးညှိ") != std::string::npos ||
                lower_query.find("အသံညှိ") != std::string::npos) {
                
                sysPrompt += "\n[ABOUT RONIN & SELF CAPABILITIES (စနစ်စွမ်းဆောင်ရည်များ)]:\n"
                             "You are Ronin (v5.0+), a sovereign on-device Cognitive AI Agent Kernel written in C++20 for Android.\n"
                             "When the user asks about your identity, capabilities, or what you can do (in Myanmar or English), explain these capabilities clearly, accurately, and proudly:\n"
                             "1. Structural Health Monitoring (SHM) & Vibration Diagnostics (အဆောက်အအုံတုန်ခါမှု တိုင်းတာစစ်ဆေးခြင်း):\n"
                             "   - 100Hz 3-axis accelerometer streaming via Welch Fast Fourier Transform (FFT) with sub-0.05Hz modal resolution.\n"
                             "   - Analyzes building/bridge/machinery resonance frequencies, Q-factor, SNR, and multi-axis coherence.\n"
                             "   - Generates engineering JSON reports and human diagnostic summaries with AI review.\n"
                             "2. Musical Instrument & Guitar Tuner (တူရိယာနှင့် ဂစ်တာအသံညှိခြင်း):\n"
                             "   - Real-time pitch analysis via microphone audio capture, Fast Fourier Transform (FFT), peak detection, and note mapping.\n"
                             "   - Standard 6-string guitar tuning (E2: 82.4Hz, A2: 110.0Hz, D3: 146.8Hz, G3: 196.0Hz, B3: 246.9Hz, E4: 329.6Hz) plus violin, ukulele, and bass.\n"
                             "   - Interactive visual pitch needle card with real-time cent deviation (±5¢ IN_TUNE, SHARP, FLAT) and haptic feedback.\n"
                             "3. Device & Hardware Controls (ဖုန်းစနစ်နှင့် Hardware ထိန်းချုပ်မှုများ):\n"
                             "   - Flashlight: Turn device flashlight on or off ('ဓာတ်မီး ဖွင့်/ပိတ်').\n"
                             "   - Connectivity: Toggle Wi-Fi and Bluetooth on/off.\n"
                             "   - Location: Read GPS coordinates and location context ('တည်နေရာ ပြပေး').\n"
                             "   - File Search: Search local storage for documents, PDFs, images, music, videos, and code scripts.\n"
                             "   - Communication: Send SMS messages and lookup Contacts (with human confirmation).\n"
                             "4. Long-Term Memory (LTM) & Secure Vault (ရေရှည်မှတ်ဉာဏ်နှင့် လုံခြုံရေး Vault):\n"
                             "   - Stores conversation history, personal notes, and learned facts in SQLite with FTS5 lexical search.\n"
                             "   - Secure Vault: Encrypted fact storage with biometric/credential protection.\n"
                             "   - Disaster Recovery: One-tap cognitive database backup to Downloads folder and restore capabilities.\n"
                             "5. Intelligent Hybrid AI Reasoning (စွမ်းရည်မြင့် ဉာဏ်ရည်တု တွေးခေါ်မှု):\n"
                             "   - On-Device: LiteRT-LM (Gemma 4) executing offline directly on Android NPU/CPU.\n"
                             "   - Multi-Cloud: Support for Google Gemini, OpenAI, OpenRouter, and Custom API endpoints.\n"
                             "   - Transparent Chain-of-Thought reasoning using [THINK] ... [/THINK] tags.\n"
                             "6. In-App Commands: /help, /capabilities, /status, /skills, /model, /reset, /reflect.\n";

                auto general_notes = m_ltm->searchNotes("capabilities");
                matched.insert(matched.end(), general_notes.begin(), general_notes.end());
                auto overview_notes = m_ltm->searchNotes("overview");
                matched.insert(matched.end(), overview_notes.begin(), overview_notes.end());
                auto tuner_notes = m_ltm->searchNotes("tuner");
                matched.insert(matched.end(), tuner_notes.begin(), tuner_notes.end());
            }

            if (!matched.empty()) {
                sysPrompt += "\n[RELEVANT KNOWLEDGE NOTES]:\n";
                std::unordered_set<std::string> seen;
                for (const auto& note : matched) {
                    if (seen.insert(note).second) {
                        sysPrompt += "- " + note + "\n";
                    }
                }
            }
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
