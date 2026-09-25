package com.ronin.kernel

import org.junit.Assert.*
import org.junit.Test
import java.io.File

class PrivacyShieldTest {

    @Test
    fun testNrcRedactionEnglish() {
        val input = "Customer NRC is 12/KAMAYA(N)123456 and spouse is 9/PAMANA(NAING)654321."
        val result = PrivacyShield.redact(input)

        assertTrue(result.hasRedactions)
        assertEquals(2, result.redactionCount)
        assertTrue(result.redactedTypes.contains(PrivacyShield.RedactionType.NRC))
        assertFalse(result.sanitizedText.contains("12/KAMAYA(N)123456"))
        assertFalse(result.sanitizedText.contains("9/PAMANA(NAING)654321"))
        assertTrue(result.sanitizedText.contains("[NRC_REDACTED]"))
    }

    @Test
    fun testNrcRedactionMyanmarScript() {
        val input = "မှတ်ပုံတင်အမှတ်မှာ ၁၂/ရကန(နိုင်)၁၂၃၄၅၆ ဖြစ်ပါသည်။"
        val result = PrivacyShield.redact(input)

        assertTrue(result.hasRedactions)
        assertEquals(1, result.redactionCount)
        assertTrue(result.redactedTypes.contains(PrivacyShield.RedactionType.NRC))
        assertFalse(result.sanitizedText.contains("၁၂/ရကန(နိုင်)၁၂၃၄၅၆"))
        assertTrue(result.sanitizedText.contains("[NRC_REDACTED]"))
    }

    @Test
    fun testMyanmarPhoneRedaction() {
        val input = "Contact me at 0912345678 or Viber +959987654321 or ၀၉၇၆၅၄၃၂၁၀."
        val result = PrivacyShield.redact(input)

        assertTrue(result.hasRedactions)
        assertTrue(result.redactedTypes.contains(PrivacyShield.RedactionType.PHONE))
        assertFalse(result.sanitizedText.contains("0912345678"))
        assertFalse(result.sanitizedText.contains("+959987654321"))
        assertFalse(result.sanitizedText.contains("၀၉၇၆၅၄၃၂၁၀"))
        assertTrue(result.sanitizedText.contains("[PHONE_REDACTED]"))
    }

    @Test
    fun testCreditCardAndBankRedaction() {
        val input = "Paid with card 4111 2222 3333 4444 and transfer to kpay: 09888777666 or account: 12345678901234."
        val result = PrivacyShield.redact(input)

        assertTrue(result.hasRedactions)
        assertTrue(result.redactedTypes.contains(PrivacyShield.RedactionType.FINANCIAL))
        assertFalse(result.sanitizedText.contains("4111 2222 3333 4444"))
        assertFalse(result.sanitizedText.contains("12345678901234"))
        assertTrue(result.sanitizedText.contains("[CARD_REDACTED]"))
        assertTrue(result.sanitizedText.contains("[ACCOUNT_REDACTED]"))
    }

    @Test
    fun testEmailAndApiKeyRedaction() {
        val input = "Send report to admin@ronin-ai.org. Use key sk-abcdef1234567890abcdef12345678 or Google AIzaSyD1234567890abcdefghijklmnopqrstu."
        val result = PrivacyShield.redact(input)

        assertTrue(result.hasRedactions)
        assertTrue(result.redactedTypes.contains(PrivacyShield.RedactionType.EMAIL))
        assertTrue(result.redactedTypes.contains(PrivacyShield.RedactionType.SECRET))
        assertFalse(result.sanitizedText.contains("admin@ronin-ai.org"))
        assertFalse(result.sanitizedText.contains("sk-abcdef1234567890abcdef12345678"))
        assertFalse(result.sanitizedText.contains("AIzaSyD1234567890abcdefghijklmnopqrstu"))
        assertTrue(result.sanitizedText.contains("[EMAIL_REDACTED]"))
        assertTrue(result.sanitizedText.contains("[SECRET_REDACTED]"))
    }

    @Test
    fun testCleanTextNotRedacted() {
        val input = "This is a regular message about building resonance frequency and structural health monitoring in Yangon."
        val result = PrivacyShield.redact(input)

        assertFalse(result.hasRedactions)
        assertEquals(0, result.redactionCount)
        assertEquals(input, result.sanitizedText)
    }

    @Test
    fun testStorageHygieneDuplicateDetection() {
        val tempDir = File(System.getProperty("java.io.tmpdir"), "hygiene_test_${System.currentTimeMillis()}")
        tempDir.mkdirs()
        try {
            val fileA = File(tempDir, "document_original.txt")
            val fileB = File(tempDir, "document_copy.txt")
            val fileC = File(tempDir, "different_file.txt")

            val identicalContent = "This is an identical document payload for duplicate testing."
            fileA.writeText(identicalContent)
            fileB.writeText(identicalContent)
            fileC.writeText("This is completely unique and different content.")

            val report = StorageHygiene.scanDirectories(listOf(tempDir))
            assertEquals(3, report.totalFilesScanned)
            assertEquals(1, report.duplicateGroups.size)
            assertEquals(2, report.duplicateGroups[0].files.size)
            assertEquals(fileA.length(), report.totalWastedBytes)
        } finally {
            tempDir.deleteRecursively()
        }
    }
}
