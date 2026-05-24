#ifndef CHAT_TEMPLATE_FORMATTER_H
#define CHAT_TEMPLATE_FORMATTER_H

#include <string>
#include <vector>

namespace Ronin::Kernel::Model {

struct ChatMessage {
    std::string role;
    std::string content;
};

class ChatTemplateFormatter {
public:
    /**
     * Phase 11.1: Gemma 4 Chat Template
     * Adheres to <start_of_turn>user\n[content]<end_of_turn>\n format.
     */
    static std::string formatGemma4(const std::vector<ChatMessage>& history, const std::string& current_prompt) {
        std::string formatted_prompt = "";

        // ၁။ Past Chat History များကို Format ချခြင်း
        for (const auto& msg : history) {
            if (msg.role == "user") {
                formatted_prompt += "<start_of_turn>user\n" + msg.content + "<end_of_turn>\n";
            } else if (msg.role == "assistant") {
                formatted_prompt += "<start_of_turn>model\n" + msg.content + "<end_of_turn>\n";
            }
        }

        // ၂။ လက်ရှိ User မေးလိုက်သော မေးခွန်းကို နောက်ဆုံးမှ ထည့်သွင်းခြင်း
        formatted_prompt += "<start_of_turn>user\n" + current_prompt + "<end_of_turn>\n";
        
        // ၃။ Model အား စတင်ဖြေဆိုရန် Target special token ပေးခြင်း
        formatted_prompt += "<start_of_turn>model\n";

        return formatted_prompt;
    }
};

} // namespace Ronin::Kernel::Model

#endif // CHAT_TEMPLATE_FORMATTER_H
