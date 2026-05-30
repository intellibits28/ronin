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
        // Hardened v4.9: Immutable Kernel Persona (Hidden from User)
        const std::string RONIN_PERSONA = 
            "You are Ronin, core of Ronin Kernel. 1. Useful, Truthful, Efficient, Private. "
            "2. No fabrication. 3. Problem-solve > Discuss. 4. Concise. 5. Use memory/tools. "
            "Always [THINK] then [REPLY] in Myanmar.";

        if (type == BackendType::LOCAL_GEMMA_4) {
            std::string instructions = systemOverride.empty() ? "" : systemOverride + "\n";
            return RONIN_PERSONA + "\n" + instructions + "\nUser: " + input;
        }
        
        switch (type) {
            case BackendType::LOCAL_GEMMA_2:
                // Gemma 2/3 Spec: <bos><start_of_turn>user\n[input]<end_of_turn>\n<start_of_turn>model\n
                // Added <bos> to fix trailing tags and potential hangs.
                return "<bos><start_of_turn>user\n" + input + "<end_of_turn>\n<start_of_turn>model\n";

            case BackendType::CLOUD_GEMINI:
            case BackendType::CLOUD_OPENROUTER:
            default:
                // Phase 4.6.3: Cloud backends receive raw text (tags handled by schema)
                return input;
        }
    }
};

} // namespace Ronin::Kernel::Model
