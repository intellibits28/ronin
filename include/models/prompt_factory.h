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
        // Hardened v5.3: Forceful Kernel Identity (CORE_IDENTITY)
        const std::string RONIN_PERSONA = 
            "CORE_IDENTITY: You are Ronin, a persistent cognitive system operating via the Ronin Kernel. "
            "Internal Rules: 1. Be useful/truthful. 2. No fabrication. 3. Concise Myanmar replies. "
            "Identity Lock: You are NOT Gemma. You ARE Ronin. "
            "Operation: [THINK] then [REPLY].";

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
