#pragma once

#include <string>

namespace Ronin::Kernel::Model {

class PromptFactory {
public:
    enum class BackendType {
        LOCAL_GEMMA_2,
        LOCAL_GEMMA_4,
        CLOUD_GEMINI,
        CLOUD_OPENROUTER
    };

    static std::string wrap(const std::string& input, BackendType type, const std::string& systemOverride = "") {
        // Hardened v5.3: Forceful Kernel Identity (CORE_IDENTITY) & Self Abilities
        const std::string RONIN_PERSONA = 
            "CORE_IDENTITY: You are Ronin, a persistent cognitive OS and agentic AI operating via a native C++20 Microkernel and Android Kotlin bridge.\n"
            "Abilities & Subsystems:\n"
            "1. Memory & Vault: Long-term fact storage, encrypted secret vault, and Myanmar FTS5 semantic recall.\n"
            "2. Sensor & Vibration: Real-time frequency-domain FFT, Structural Resonance analysis, and Impulse Capture (tapping/impact detection).\n"
            "3. Audio & Pitch: String fundamental frequency detection and guitar tuning.\n"
            "4. Assistant & Planning: Calendar event querying/adding, alarm setting, location & map navigation, SMS/email dispatch.\n"
            "5. Governance & Reflection: Bayesian Self-Correction, evolutionary behavioral lesson learning, and Macro-Skill discovery.\n"
            "Internal Rules: 1. Be useful and truthful. 2. No fabrication. 3. Concise Myanmar replies.\n"
            "Identity Lock: You are NOT Gemma. You ARE Ronin.\n"
            "Operation: Reason in [THINK] [/THINK] then reply in [REPLY] [/REPLY].";

        if (type == BackendType::LOCAL_GEMMA_4) {
            std::string instructions = systemOverride.empty() ? "" : systemOverride + "\n";
            return RONIN_PERSONA + "\n" + instructions + "\nUser: " + input;
        }
        
        switch (type) {
            case BackendType::LOCAL_GEMMA_2: {
                std::string instructions = systemOverride.empty() ? "" : systemOverride + "\n";
                return "<bos><start_of_turn>user\n" + RONIN_PERSONA + "\n" + instructions + "\nUser: " + input + "<end_of_turn>\n<start_of_turn>model\n";
            }

            case BackendType::CLOUD_GEMINI:
            case BackendType::CLOUD_OPENROUTER:
            default:
                // Phase 4.6.3: Cloud backends receive raw text (tags handled by schema)
                return input;
        }
    }
};

} // namespace Ronin::Kernel::Model
