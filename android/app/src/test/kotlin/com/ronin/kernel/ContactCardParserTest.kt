package com.ronin.kernel

import com.ronin.kernel.ContactCardParser.ExtractedPhone
import com.ronin.kernel.ContactCardParser.ParsedContactCandidate
import com.ronin.kernel.ContactCardParser.PhoneLabel
import com.ronin.kernel.ContactManager.ExistingContactRecord
import org.junit.Assert.*
import org.junit.Test

class ContactCardParserTest {

    @Test
    fun testFacebookScreenshotOneNameTwoPhonesWithNoise() {
        val ocrText = """
            8:21 AM
            Ko Kyaw Zin
            2h •
            09 799 123 456
            09-420-987-654
            Like  Comment  Share
        """.trimIndent()

        val parsed = ContactCardParser.parse(ocrText)
        assertTrue(parsed.hasContactInfo)
        assertEquals("Ko Kyaw Zin", parsed.displayName)
        assertEquals(2, parsed.phones.size)
        assertEquals("09799123456", parsed.phones[0].number)
        assertEquals("09420987654", parsed.phones[1].number)
    }

    @Test
    fun testFullBusinessCardBilingualParsing() {
        val cardText = """
            U Aung Min
            Senior Structural Engineer
            Apex Engineering Co., Ltd
            Tel: +95 9 512 3456, 09-798-112233
            Fax: 01-234567
            Email: aungmin@apex.com.mm
            Web: www.apex-engineering.com.mm
            No. 45, Pyay Road, Kamayut Township, Yangon
        """.trimIndent()

        val parsed = ContactCardParser.parse(cardText)
        assertTrue(parsed.isBusinessCardLikely)
        assertEquals("U Aung Min", parsed.displayName)
        assertEquals("Senior Structural Engineer", parsed.jobTitle)
        assertEquals("Apex Engineering Co., Ltd", parsed.organization)
        assertEquals(3, parsed.phones.size)
        assertEquals("+9595123456", parsed.phones[0].number)
        assertEquals("09798112233", parsed.phones[1].number)
        assertEquals("01234567", parsed.phones[2].number)
        assertEquals(PhoneLabel.FAX, parsed.phones[2].label)
        assertEquals(listOf("aungmin@apex.com.mm"), parsed.emails)
        assertEquals("www.apex-engineering.com.mm", parsed.website)
        assertTrue(parsed.address.contains("Yangon"))
    }

    @Test
    fun testBurmeseNameAndBurmeseDigitsParsing() {
        val ocrText = """
            ဦးသန့်ဇော်
            ဖုန်း - ၀၉ ၇၉၉ ၁၂၃ ၄၅၆
            ၀၉-၄၂၀-၉၈၇-၆၅၄
        """.trimIndent()

        val parsed = ContactCardParser.parse(ocrText)
        assertEquals("ဦးသန့်ဇော်", parsed.displayName)
        assertEquals(2, parsed.phones.size)
        assertEquals("09799123456", parsed.phones[0].number)
        assertEquals("09420987654", parsed.phones[1].number)
    }

    @Test
    fun testOcrZeroLetterArtifactRepairOnPhone() {
        val ocrText = "Ma Hnin\nO9 799 123 456"
        val parsed = ContactCardParser.parse(ocrText)
        assertEquals("Ma Hnin", parsed.displayName)
        assertEquals(1, parsed.phones.size)
        assertEquals("09799123456", parsed.phones[0].number)
    }

    @Test
    fun testNaturalLanguageContactCommandPreservesBurmeseHonorificKo() {
        val cmd = "ကိုကျော်ဇင် 09799123456 09420987654 ကို new contact ထည့်ပေးပါ"
        val parsed = ContactCardParser.parseNaturalContactCommand(cmd)
        assertEquals("ကိုကျော်ဇင်", parsed.displayName)
        assertEquals(2, parsed.phones.size)
        assertEquals("09799123456", parsed.phones[0].number)
        assertEquals("09420987654", parsed.phones[1].number)
    }

    @Test
    fun testContactManagerPhoneMatchAndUpdateDiff() {
        assertTrue(ContactManager.phonesMatch("+95 9 799 123 456", "09799123456"))
        assertTrue(ContactManager.phonesMatch("၀၉-၇၉၉-၁၂၃၄၅၆", "09799123456"))
        assertFalse(ContactManager.phonesMatch("09799123456", "09420987654"))

        val existingList = listOf(
            ExistingContactRecord(
                contactId = 10L,
                displayName = "Other Person",
                phones = listOf("09111222333")
            ),
            ExistingContactRecord(
                contactId = 22L,
                displayName = "Ko Kyaw Zin",
                phones = listOf("+959799123456")
            )
        )

        val candidate = ParsedContactCandidate(
            displayName = "Ko Kyaw Zin",
            phones = listOf(
                ExtractedPhone("09799123456", label = PhoneLabel.MOBILE),
                ExtractedPhone("09420987654", label = PhoneLabel.MOBILE)
            ),
            emails = listOf("kyawzin@example.com")
        )

        val matches = ContactManager.matchContactsAgainstList(candidate, existingList)
        assertEquals(1, matches.size)
        assertEquals(22L, matches[0].contactId)
        assertEquals(100, matches[0].matchScore)

        val diff = ContactManager.computeUpdateDiff(matches[0], candidate)
        assertTrue(diff.hasNewChanges)
        assertEquals(1, diff.duplicatePhones.size)
        assertEquals("09799123456", diff.duplicatePhones[0].number)
        assertEquals(1, diff.phonesToAdd.size)
        assertEquals("09420987654", diff.phonesToAdd[0].number)
        assertEquals(listOf("kyawzin@example.com"), diff.emailsToAdd)
        assertNull(diff.updatedDisplayName)
    }
}
