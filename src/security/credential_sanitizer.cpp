#include "security/credential_sanitizer.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Ronin::Kernel::Security {

namespace {

const std::vector<std::pair<std::regex, std::string>>& getSecretPatterns() {
    static const std::vector<std::pair<std::regex, std::string>> s_patterns = {
        // 1. Google API Keys (e.g. AIzaSyD...)
        {std::regex("AIza[0-9A-Za-z\\-_]{35}"), "[REDACTED_SECRET]"},

        // 2. OpenAI / Anthropic / Generic sk- keys (e.g. sk-ant-..., sk-proj-...)
        {std::regex("sk-[A-Za-z0-9\\-_]{20,}"), "[REDACTED_SECRET]"},

        // 3. GitHub personal access & app tokens
        {std::regex("(ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36,}"), "[REDACTED_SECRET]"},

        // 4. Authorization Bearer / Basic tokens
        {std::regex("(bearer|basic)\\s+[A-Za-z0-9\\-._~+/]+=*", std::regex_constants::icase), "$1 [REDACTED_SECRET]"},

        // 5. English assignment markers with long alphanumeric secret strings
        {std::regex("\\b(to|as|with|is)\\s+([A-Za-z0-9\\-_]{16,})", std::regex_constants::icase), "$1 [REDACTED_SECRET]"},

        // 6. Burmese assignment markers: <secret> အဖြစ် / လို့ / သို့
        {std::regex("([A-Za-z0-9\\-_]{16,})\\s*(အဖြစ်|လို့|သို့)"), "[REDACTED_SECRET] $2"},

        // 7. Burmese prefix assignment markers: သို့ / အဖြစ် <secret>
        {std::regex("(အဖြစ်|သို့|လို့)\\s*([A-Za-z0-9\\-_]{16,})"), "$1 [REDACTED_SECRET]"}
    };
    return s_patterns;
}

} // namespace

bool CredentialSanitizer::isSensitiveKey(const std::string& key) {
    std::string kl = key;
    std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
    return (
        kl.find("password") != std::string::npos ||
        kl.find("vault_content") != std::string::npos ||
        kl.find("secret") != std::string::npos ||
        kl.find("key") != std::string::npos ||
        kl.find("token") != std::string::npos ||
        kl.find("bearer") != std::string::npos ||
        kl.find("credential") != std::string::npos ||
        kl.find("auth") != std::string::npos ||
        kl.find("apikey") != std::string::npos
    );
}

std::string CredentialSanitizer::sanitizeText(const std::string& input) {
    if (input.empty()) return input;

    std::string out = input;
    for (const auto& [pattern, replacement] : getSecretPatterns()) {
        try {
            out = std::regex_replace(out, pattern, replacement);
        } catch (...) {
            // Safe fallback if regex matching fails on malformed input
        }
    }
    return out;
}

std::string CredentialSanitizer::sanitizeParam(const std::string& key, const std::string& value) {
    if (isSensitiveKey(key) && !value.empty()) {
        return REDACTED_MARKER;
    }
    return sanitizeText(value);
}

std::string CredentialSanitizer::sanitizeJson(const std::string& json_str) {
    if (json_str.empty()) return json_str;

    try {
        auto j = nlohmann::json::parse(json_str);
        auto sanitizeObj = [](auto& self, nlohmann::json& node) -> void {
            if (node.is_object()) {
                for (auto it = node.begin(); it != node.end(); ++it) {
                    if (isSensitiveKey(it.key())) {
                        it.value() = REDACTED_MARKER;
                    } else if (it.value().is_string()) {
                        it.value() = sanitizeText(it.value().get<std::string>());
                    } else if (it.value().is_object() || it.value().is_array()) {
                        self(self, it.value());
                    }
                }
            } else if (node.is_array()) {
                for (auto& item : node) {
                    if (item.is_string()) {
                        item = sanitizeText(item.get<std::string>());
                    } else if (item.is_object() || item.is_array()) {
                        self(self, item);
                    }
                }
            }
        };
        sanitizeObj(sanitizeObj, j);
        return j.dump();
    } catch (...) {
        // Not valid JSON, apply text sanitizer directly
        return sanitizeText(json_str);
    }
}

} // namespace Ronin::Kernel::Security
