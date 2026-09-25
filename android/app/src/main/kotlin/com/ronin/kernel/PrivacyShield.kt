package com.ronin.kernel

import android.util.Log

/**
 * Tier 4: Privacy Shield & Data Redaction Engine.
 * Detects and redacts Personally Identifiable Information (PII) before:
 * 1. Outgoing Cloud LLM prompts (Gemini, OpenRouter)
 * 2. OCR text extraction & indexing
 * 3. Telemetry and Chat Export logs
 *
 * Supports both Myanmar (Unicode & script) and English PII patterns.
 */
object PrivacyShield {
    private const val TAG = "Ronin_PrivacyShield"

    enum class RedactionType {
        NRC,
        PHONE,
        FINANCIAL,
        EMAIL,
        SECRET,
        CREDENTIAL
    }

    data class PrivacyConfig(
        val redactNrc: Boolean = true,
        val redactPhone: Boolean = true,
        val redactFinancial: Boolean = true,
        val redactEmail: Boolean = true,
        val redactSecrets: Boolean = true,
        val redactCredentials: Boolean = true
    ) {
        companion object {
            val DEFAULT = PrivacyConfig()
            val STRICT = PrivacyConfig(
                redactNrc = true,
                redactPhone = true,
                redactFinancial = true,
                redactEmail = true,
                redactSecrets = true,
                redactCredentials = true
            )
            val DISABLED = PrivacyConfig(
                redactNrc = false,
                redactPhone = false,
                redactFinancial = false,
                redactEmail = false,
                redactSecrets = false,
                redactCredentials = false
            )
        }
    }

    data class RedactionOccurrence(
        val type: RedactionType,
        val placeholder: String,
        val originalText: String,
        val startOffset: Int,
        val endOffset: Int
    )

    data class RedactionResult(
        val originalText: String,
        val sanitizedText: String,
        val hasRedactions: Boolean,
        val redactionCount: Int,
        val redactedTypes: Set<RedactionType>,
        val occurrences: List<RedactionOccurrence>
    )

    // 1. Myanmar National Registration Card (NRC)
    // Matches English: 12/KAMAYA(N)123456, 9/PAMANA(NAING)654321
    // Matches Burmese: ၁၂/ရကန(နိုင်)၁၂၃၄၅၆, ၉/မမန(ဧည့်)၁၂၃၄၅၆
    // Handles various spacing and bracket variations: (နိုင်), [နိုင်], (N), (NAING)
    private val NRC_REGEX = Regex(
        """(?:[0-9]{1,2}|[\u1040-\u1049]{1,2})\s*/\s*[A-Za-z\u1000-\u104F\s]+\s*(?:\((?:နိုင်|ဧည့်|ပြု|သ|N|P|E|NAING|AING)\)|\[(?:နိုင်|ဧည့်|ပြု|သ|N|P|E|NAING|AING)\]|\b(?:နိုင်|ဧည့်|ပြု|သ|N|P|E)\b)\s*(?:[0-9]{6}|[\u1040-\u1049]{6})""",
        RegexOption.IGNORE_CASE
    )

    // Simplified fallback NRC pattern for cases without explicit bracket code
    private val NRC_SIMPLIFIED_REGEX = Regex(
        """\b(?:[0-9]{1,2}|[\u1040-\u1049]{1,2})/[A-Za-z\u1000-\u104F]+(?:\([A-Za-z\u1000-\u104F]+\))?(?:[0-9]{6}|[\u1040-\u1049]{6})\b"""
    )

    // 2. Myanmar Phone Numbers (+959..., 09..., +၉၅၉..., ၀၉...)
    private val MYANMAR_PHONE_REGEX = Regex(
        """(?<=\A|[\s,.:;!?'"()\[\]])(?:\+?959|09|\+?၉၅၉|၀၉)[- ]?(?:[0-9]{7,9}|[\u1040-\u1049]{7,9})(?=\z|[\s,.:;!?'"()\[\]])"""
    )

    // International Phone with labels (Tel:, Phone:, Ph:)
    private val LABELED_PHONE_REGEX = Regex(
        """(?i:\b(?:tel|phone|mobile|call|ph|cell|viber)[:\s]+)(\+?[0-9]{7,15})\b"""
    )

    // 3. Email Addresses
    private val EMAIL_REGEX = Regex(
        """\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b"""
    )

    // 4. Financial & Payment Cards (Visa, MasterCard, Amex: 13-16 digits with optional spaces/hyphens)
    private val CREDIT_CARD_REGEX = Regex(
        """\b(?:\d{4}[ -]?){3}\d{4}\b|\b3[47]\d{2}[ -]?\d{6}[ -]?\d{5}\b"""
    )

    // Bank Accounts & Mobile Wallets (KBZPay, WavePay, CB, AYA, Account #)
    private val BANK_ACCOUNT_REGEX = Regex(
        """(?i:\b(?:kpay|wave|wavepay|kbzpay|account|acct|bank|mtransfer|cbbank|ayabank|uab|yoma)[:\s#]+)(\d{8,18})\b"""
    )

    // 5. API Keys, Tokens & Secrets
    // Matches Google API Keys (AIza...), OpenAI/OpenRouter (sk-...), and generic tokens
    private val API_KEY_REGEX = Regex(
        """\bAIza[0-9A-Za-z\-_]{30,45}\b|\bsk-[a-zA-Z0-9_\-]{20,}\b|(?i:\b(?:bearer|api[_-]?key|access[_-]?token|auth[_-]?token|secret[_-]?key)[:=\s]+)([a-zA-Z0-9_\-.]{16,})"""
    )

    // 6. Credentials & Passwords
    private val PASSWORD_REGEX = Regex(
        """(?i:\b(?:password|passwd|pwd|passcode|secret_pin)[:=\s]+)(\S+)"""
    )

    /**
     * Sanitizes input text by applying regex redactions according to the provided config.
     */
    fun redact(text: String, config: PrivacyConfig = PrivacyConfig.DEFAULT): RedactionResult {
        if (text.isBlank()) {
            return RedactionResult(text, text, false, 0, emptySet(), emptyList())
        }

        var currentText = text
        val occurrences = mutableListOf<RedactionOccurrence>()
        val typesFound = mutableSetOf<RedactionType>()

        // 1. NRC Redaction
        if (config.redactNrc) {
            currentText = redactPattern(currentText, NRC_REGEX, "[NRC_REDACTED]", RedactionType.NRC, occurrences, typesFound)
            currentText = redactPattern(currentText, NRC_SIMPLIFIED_REGEX, "[NRC_REDACTED]", RedactionType.NRC, occurrences, typesFound)
        }

        // 2. Secrets & API Keys (Run before credentials and bank to prevent token fragmentation)
        if (config.redactSecrets) {
            currentText = redactPattern(currentText, API_KEY_REGEX, "[SECRET_REDACTED]", RedactionType.SECRET, occurrences, typesFound)
        }

        // 3. Credentials & Passwords
        if (config.redactCredentials) {
            currentText = redactPattern(currentText, PASSWORD_REGEX, "[CREDENTIAL_REDACTED]", RedactionType.CREDENTIAL, occurrences, typesFound, matchGroupIndex = 1)
        }

        // 4. Financial (Credit Card & Bank / Wallets)
        if (config.redactFinancial) {
            currentText = redactPattern(currentText, CREDIT_CARD_REGEX, "[CARD_REDACTED]", RedactionType.FINANCIAL, occurrences, typesFound)
            currentText = redactPattern(currentText, BANK_ACCOUNT_REGEX, "[ACCOUNT_REDACTED]", RedactionType.FINANCIAL, occurrences, typesFound, matchGroupIndex = 1)
        }

        // 5. Phone Numbers
        if (config.redactPhone) {
            currentText = redactPattern(currentText, MYANMAR_PHONE_REGEX, "[PHONE_REDACTED]", RedactionType.PHONE, occurrences, typesFound)
            currentText = redactPattern(currentText, LABELED_PHONE_REGEX, "[PHONE_REDACTED]", RedactionType.PHONE, occurrences, typesFound, matchGroupIndex = 1)
        }

        // 6. Emails
        if (config.redactEmail) {
            currentText = redactPattern(currentText, EMAIL_REGEX, "[EMAIL_REDACTED]", RedactionType.EMAIL, occurrences, typesFound)
        }

        val hasRedactions = occurrences.isNotEmpty()
        if (hasRedactions) {
            Log.i(TAG, "Redacted ${occurrences.size} sensitive items (${typesFound.joinToString()})")
        }

        return RedactionResult(
            originalText = text,
            sanitizedText = currentText,
            hasRedactions = hasRedactions,
            redactionCount = occurrences.size,
            redactedTypes = typesFound,
            occurrences = occurrences
        )
    }

    /**
     * Helper to redact matches of a specific regex.
     * When matchGroupIndex is specified, only that capturing group is replaced (e.g., keeping "password: ").
     */
    private fun redactPattern(
        input: String,
        regex: Regex,
        placeholder: String,
        type: RedactionType,
        occurrences: MutableList<RedactionOccurrence>,
        typesFound: MutableSet<RedactionType>,
        matchGroupIndex: Int = 0
    ): String {
        return regex.replace(input) { matchResult ->
            val targetGroup = if (matchGroupIndex > 0 && matchResult.groups.size > matchGroupIndex && matchResult.groups[matchGroupIndex] != null) {
                matchResult.groups[matchGroupIndex]!!
            } else {
                null
            }

            val originalSegment = targetGroup?.value ?: matchResult.value
            occurrences.add(
                RedactionOccurrence(
                    type = type,
                    placeholder = placeholder,
                    originalText = originalSegment,
                    startOffset = targetGroup?.range?.first ?: matchResult.range.first,
                    endOffset = targetGroup?.range?.last ?: matchResult.range.last
                )
            )
            typesFound.add(type)

            if (targetGroup != null) {
                val fullMatch = matchResult.value
                val groupStartInMatch = targetGroup.range.first - matchResult.range.first
                val groupEndInMatch = targetGroup.range.last - matchResult.range.first + 1
                fullMatch.substring(0, groupStartInMatch) + placeholder + fullMatch.substring(groupEndInMatch)
            } else {
                placeholder
            }
        }
    }

    /**
     * Returns true if text contains any PII item according to the config.
     */
    fun isSensitive(text: String, config: PrivacyConfig = PrivacyConfig.DEFAULT): Boolean {
        if (text.isBlank()) return false
        if (config.redactNrc && (NRC_REGEX.containsMatchIn(text) || NRC_SIMPLIFIED_REGEX.containsMatchIn(text))) return true
        if (config.redactSecrets && API_KEY_REGEX.containsMatchIn(text)) return true
        if (config.redactCredentials && PASSWORD_REGEX.containsMatchIn(text)) return true
        if (config.redactFinancial && (CREDIT_CARD_REGEX.containsMatchIn(text) || BANK_ACCOUNT_REGEX.containsMatchIn(text))) return true
        if (config.redactPhone && (MYANMAR_PHONE_REGEX.containsMatchIn(text) || LABELED_PHONE_REGEX.containsMatchIn(text))) return true
        if (config.redactEmail && EMAIL_REGEX.containsMatchIn(text)) return true
        return false
    }

    /**
     * Formats a human-readable summary of redaction results.
     */
    fun formatSummary(result: RedactionResult): String {
        if (!result.hasRedactions) return "✅ No sensitive PII detected."
        val typeList = result.redactedTypes.joinToString(", ") { it.name }
        return "🛡️ Redacted ${result.redactionCount} items: [$typeList]"
    }
}
