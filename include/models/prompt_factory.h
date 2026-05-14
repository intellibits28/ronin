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
                // Gemma 4 Official Jinja: <bos><|turn>system\n[sys]<turn|>\n<|turn>user\n[input]<turn|>\n<|turn>model\n
                // Note: Adding an extra newline after <|turn>model\n to ensure trigger.
                return "<bos><|turn>system\nYou are Ronin, a sentient edge AI.<turn|>\n"
                       "<|turn>user\n" + input + "<turn|>\n"
                       "<|turn>model\n\n";
            
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
