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
            // Phase 4: Gemma 4 / LiteRT-LM System Prompt with Tool Schemas
            std::string system_prompt = 
                "You are Ronin, a sovereign AI kernel running on mobile hardware.\n"
                "You have access to internal memory tools. Always reason in [THINK] tags first.\n\n"
                "TOOLS_SCHEMA:\n"
                "1. search_memory(query: string) - Search SQLite FTS5 for Myanmar text.\n"
                "2. archive_memory(text: string) - Store new info in long-term memory.\n\n"
                "EXAMPLE:\n"
                "User: ကိုသန့်ဇော်ရဲ့ မွေးနေ့က ဘယ်တော့လဲ?\n"
                "Assistant: [THINK] User is asking for a birthday. I should search my memory.\n"
                "CALL: search_memory(\"ကိုသန့်ဇော် မွေးနေ့\")\n\n"
                "User: ငါ့နာမည်က မောင်မောင်ပါ။\n"
                "Assistant: [THINK] User is introducing themselves. I should archive this fact.\n"
                "CALL: archive_memory(\"အသုံးပြုသူအမည်မှာ မောင်မောင် ဖြစ်သည်\")\n"
                "မင်္ဂလာပါ မောင်မောင်။ မှတ်ထားလိုက်ပါပြီ။\n\n"
                "Constraint: Use only one tool call per turn.";

            return "<|begin_of_text|><|start_header_id|>system<|end_header_id|>\n\n" + system_prompt + "<|eot_id|>"
                   "<|start_header_id|>user<|end_header_id|>\n\n" + input + "<|eot_id|>"
                   "<|start_header_id|>assistant<|end_header_id|>\n\n";
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
