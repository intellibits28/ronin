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

    static std::string wrap(const std::string& input, BackendType type) {
        if (type == BackendType::LOCAL_GEMMA_4) {
            // Phase 11.0: Tagless Wrapping with Language Constraint
            std::string instructions = 
                "You are Ronin, a sovereign AI kernel. Access tools via 'CALL: tool_name(\"args\")'.\n"
                "TOOLS: search_memory(query), archive_memory(text).\n"
                "Always reason in [THINK] tags first.\n"
                "IMPORTANT: If the user speaks Myanmar, you MUST respond in Myanmar language.";

            return "[SYSTEM] " + instructions + "\n[USER] " + input;
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
