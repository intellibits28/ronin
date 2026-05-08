#pragma once

#include <string>

namespace Ronin::Kernel::Model {

class PromptFactory {
public:
    enum class BackendType {
        LOCAL_GEMMA,
        CLOUD_GEMINI,
        CLOUD_OPENROUTER
    };

    static std::string wrap(const std::string& input, BackendType type) {
        switch (type) {
            case BackendType::LOCAL_GEMMA:
                // Phase 3: Gemma 4 Specialized Turn-based Formatting
                return "<|turn>system\nYou are Ronin, a sentient edge AI.\n<turn|>\n<|turn>user\n" + input + "<turn|>\n<|turn>model\n";
            
            case BackendType::CLOUD_GEMINI:
            case BackendType::CLOUD_OPENROUTER:
            default:
                // Phase 4.6.3: Cloud backends receive raw text (tags handled by schema)
                return input;
        }
    }
};

} // namespace Ronin::Kernel::Model
