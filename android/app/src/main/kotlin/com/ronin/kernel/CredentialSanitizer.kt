package com.ronin.kernel

/**
 * Global Credential Redaction Scanner (Phase 1 / Architectural Refinement 4).
 * Combines key-name masking with value-based regex token scanning to prevent secret leakage in logs.
 */
object CredentialSanitizer {
    const val REDACTED_MARKER = "[REDACTED_SECRET]"

    private val secretPatterns = listOf(
        // 1. Google API keys (AIza...)
        Regex("AIza[0-9A-Za-z\\-_]{35}"),
        // 2. OpenAI / Anthropic / Generic sk- tokens
        Regex("sk-[A-Za-z0-9\\-_]{20,}"),
        // 3. GitHub personal access & app tokens
        Regex("(ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36,}"),
        // 4. Authorization Bearer / Basic tokens
        Regex("(?i)(bearer|basic)\\s+[A-Za-z0-9\\-._~+/]+=*"),
        // 5. English assignment markers: to/as/with/is <secret>
        Regex("(?i)\\b(to|as|with|is)\\s+([A-Za-z0-9\\-_]{16,})"),
        // 6. Burmese assignment markers: <secret> အဖြစ် / လို့ / သို့
        Regex("([A-Za-z0-9\\-_]{16,})\\s*(အဖြစ်|လို့|သို့)"),
        // 7. Burmese prefix: အဖြစ် / သို့ / လို့ <secret>
        Regex("(အဖြစ်|သို့|လို့)\\s*([A-Za-z0-9\\-_]{16,})")
    )

    fun isSensitiveKey(key: String): Boolean {
        val kl = key.lowercase()
        return kl.contains("password") || kl.contains("vault_content") || kl.contains("secret") ||
               kl.contains("key") || kl.contains("token") || kl.contains("bearer") ||
               kl.contains("credential") || kl.contains("auth") || kl.contains("apikey")
    }

    fun sanitizeText(input: String): String {
        if (input.isEmpty()) return input
        var out = input
        for (p in secretPatterns) {
            out = p.replace(out) { matchResult ->
                val v = matchResult.value
                when {
                    v.startsWith("bearer ", true) -> "Bearer $REDACTED_MARKER"
                    v.startsWith("basic ", true) -> "Basic $REDACTED_MARKER"
                    v.startsWith("to ", true) -> "to $REDACTED_MARKER"
                    v.startsWith("as ", true) -> "as $REDACTED_MARKER"
                    v.startsWith("with ", true) -> "with $REDACTED_MARKER"
                    v.startsWith("is ", true) -> "is $REDACTED_MARKER"
                    v.endsWith("အဖြစ်") -> "$REDACTED_MARKER အဖြစ်"
                    v.endsWith("လို့") -> "$REDACTED_MARKER လို့"
                    v.endsWith("သို့") -> "$REDACTED_MARKER သို့"
                    v.startsWith("အဖြစ်") -> "အဖြစ် $REDACTED_MARKER"
                    v.startsWith("သို့") -> "သို့ $REDACTED_MARKER"
                    v.startsWith("လို့") -> "လို့ $REDACTED_MARKER"
                    else -> REDACTED_MARKER
                }
            }
        }
        return out
    }

    fun sanitizeParam(key: String, value: String): String {
        return if (isSensitiveKey(key) && value.isNotEmpty()) REDACTED_MARKER else sanitizeText(value)
    }
}
