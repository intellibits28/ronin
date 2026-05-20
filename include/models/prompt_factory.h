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
                // Phase 4.6.8: Gemma 4 / LiteRT-LM Prompt Formatting.
                // Even with SDK-side tagging, adding explicit headers ensures response hydration
                // for models converted from Llama-3 or Gemma-3/4 base weights.
                return "<|begin_of_text|><|start_header_id|>user<|end_header_id|>\n\n" + input + "<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n\n";
            
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
