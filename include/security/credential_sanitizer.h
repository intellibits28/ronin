#pragma once

#include <string>
#include <vector>
#include <regex>

namespace Ronin::Kernel::Security {

/**
 * Universal Value-Based and Key-Based Credential Sanitizer.
 * Prevents raw secrets from leaking into debug monitors, logs, or telemetry buses.
 */
class CredentialSanitizer {
public:
    /**
     * Scans arbitrary text for secret tokens (Google AIza, OpenAI sk-, GitHub tokens,
     * Bearer auth headers, high-entropy assignment tokens) and redacts them in-place.
     */
    static std::string sanitizeText(const std::string& input);

    /**
     * Sanitizes a JSON string by checking both sensitive field names and values.
     */
    static std::string sanitizeJson(const std::string& json_str);

    /**
     * Sanitizes a specific key-value pair.
     */
    static std::string sanitizeParam(const std::string& key, const std::string& value);

    /**
     * Returns true if the key name indicates a sensitive credential.
     */
    static bool isSensitiveKey(const std::string& key);

    /**
     * The canonical mask string for redacted secrets.
     */
    static constexpr const char* REDACTED_MARKER = "[REDACTED_SECRET]";
};

} // namespace Ronin::Kernel::Security
