package com.ronin.kernel

import org.junit.Assert.*
import org.junit.Test
import org.json.JSONObject
import java.io.File

/**
 * Unit tests for Semantic Intent & Credential Redaction Safety (Ronin Kernel v5.1.x Phase 1-4).
 */
class SemanticRoutingSafetyTest {

    @Test
    fun testCredentialSanitizerRedactsGoogleApiKey() {
        val input = "Please update key to AIzaSyD1234567890abcdefghijklmnopqrst in vault"
        val sanitized = CredentialSanitizer.sanitizeText(input)
        assertFalse(sanitized.contains("AIzaSyD1234567890abcdefghijklmnopqrst"))
        assertTrue(sanitized.contains("[REDACTED_SECRET]"))
    }

    @Test
    fun testCredentialSanitizerRedactsOpenAiKey() {
        val input = "Use key sk-abcdef1234567890abcdefghijklmnopqrstuv for inference"
        val sanitized = CredentialSanitizer.sanitizeText(input)
        assertFalse(sanitized.contains("sk-abcdef1234567890abcdefghijklmnopqrstuv"))
        assertTrue(sanitized.contains("[REDACTED_SECRET]"))
    }

    @Test
    fun testCredentialSanitizerRedactsGitHubTokens() {
        val input = "Clone repo using token ghp_123456789012345678901234567890123456"
        val sanitized = CredentialSanitizer.sanitizeText(input)
        assertFalse(sanitized.contains("ghp_123456789012345678901234567890123456"))
        assertTrue(sanitized.contains("[REDACTED_SECRET]"))
    }

    @Test
    fun testCredentialSanitizerRedactsBearerTokens() {
        val input = "Authorization: Bearer secretBearerToken1234567890"
        val sanitized = CredentialSanitizer.sanitizeText(input)
        assertFalse(sanitized.contains("secretBearerToken1234567890"))
        assertTrue(sanitized.contains("[REDACTED_SECRET]"))
    }

    @Test
    fun testCredentialSanitizerRedactsBurmeseAssignment() {
        val input = "gemini api key ကို AIzaSyD1234567890abcdefghijklmnopqrst အဖြစ် ပြောင်းပေးပါ"
        val sanitized = CredentialSanitizer.sanitizeText(input)
        assertFalse(sanitized.contains("AIzaSyD1234567890abcdefghijklmnopqrst"))
        assertTrue(sanitized.contains("[REDACTED_SECRET]"))
    }

    @Test
    fun testCredentialSanitizerSensitiveKeyMasking() {
        assertTrue(CredentialSanitizer.isSensitiveKey("vault_content"))
        assertTrue(CredentialSanitizer.isSensitiveKey("api_key"))
        assertTrue(CredentialSanitizer.isSensitiveKey("password"))
        assertTrue(CredentialSanitizer.isSensitiveKey("auth_token"))

        val masked = CredentialSanitizer.sanitizeParam("vault_content", "anyPlaintextPassword123")
        assertEquals("[REDACTED_SECRET]", masked)
    }

    @Test
    fun testIntentExemplarsJsonIntegrity() {
        val assetFile = File("src/main/assets/intent_exemplars.json")
        val fileToRead = if (assetFile.exists()) assetFile else File("android/app/src/main/assets/intent_exemplars.json")
        assertTrue("intent_exemplars.json must exist in assets", fileToRead.exists())

        val jsonStr = fileToRead.readText()
        val root = JSONObject(jsonStr)
        assertEquals(2, root.getInt("schema_version"))
        assertEquals("embeddinggemma-2-270m", root.getString("model_target"))
        assertEquals(768, root.getInt("embedding_dim"))

        val intentsArray = root.getJSONArray("intents")
        assertTrue("Must contain at least 11 intent clusters", intentsArray.length() >= 11)

        val intentNames = mutableSetOf<String>()
        for (i in 0 until intentsArray.length()) {
            val obj = intentsArray.getJSONObject(i)
            val name = obj.getString("name")
            intentNames.add(name)

            val registers = obj.getJSONObject("registers")
            assertTrue("Register EN must exist for $name", registers.has("EN"))
            assertTrue("Register MM must exist for $name", registers.has("MM"))
            assertTrue("Register MIX must exist for $name", registers.has("MIX"))

            assertTrue("EN exemplars must not be empty for $name", registers.getJSONArray("EN").length() > 0)
            assertTrue("MM exemplars must not be empty for $name", registers.getJSONArray("MM").length() > 0)
            assertTrue("MIX exemplars must not be empty for $name", registers.getJSONArray("MIX").length() > 0)
        }

        assertTrue("VAULT_LOOKUP must exist", intentNames.contains("VAULT_LOOKUP"))
        assertTrue("VAULT_UPDATE must exist", intentNames.contains("VAULT_UPDATE"))
        assertTrue("VAULT_SAVE must exist", intentNames.contains("VAULT_SAVE"))
        assertTrue("FLASHLIGHT must exist", intentNames.contains("FLASHLIGHT"))
    }
}
