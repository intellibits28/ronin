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
        switch (type) {
            case BackendType::LOCAL_GEMMA_4:
                // Phase 3: Gemma 4 Specialized Turn-based Formatting (Spec: <bos><|turn>role\n...<turn|>\n)
                return "<bos><|turn>system\nYou are Ronin, a sentient edge AI.<turn|>\n<|turn>user\n" + input + "<turn|>\n<|turn>model\n\n";
            
            case BackendType::LOCAL_GEMMA_2:
                // Legacy: Gemma 2/3 Formatting
                return "<start_of_turn>user\n" + input + "<end_of_turn>\n<start_of_turn>model\n";

            case BackendType::CLOUD_GEMINI:
            case BackendType::CLOUD_OPENROUTER:
            default:
                // Phase 4.6.3: Cloud backends receive raw text (tags handled by schema)
                return input;
        }
    }
};

} // namespace Ronin::Kernel::Model
