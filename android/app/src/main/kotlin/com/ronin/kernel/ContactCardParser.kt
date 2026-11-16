package com.ronin.kernel

/**
 * Extracts structured contact & business card fields from OCR text or pasted messages.
 * Supports Myanmar & English names, multi-number lines (slash/comma separated),
 * Burmese Unicode numerals (၀-၉), emails, organizations, job titles, websites, and addresses.
 */
object ContactCardParser {

    enum class PhoneLabel(val displayLabel: String) {
        MOBILE("Mobile"),
        WORK("Work"),
        HOME("Home"),
        FAX("Fax"),
        OTHER("Other")
    }

    data class ExtractedPhone(
        val number: String,
        val rawMatch: String = number,
        val label: PhoneLabel = PhoneLabel.MOBILE
    )

    data class ParsedContactCandidate(
        val displayName: String,
        val phones: List<ExtractedPhone>,
        val emails: List<String> = emptyList(),
        val organization: String = "",
        val jobTitle: String = "",
        val address: String = "",
        val website: String = "",
        val notes: String = "",
        val rawSourceText: String = "",
        val isBusinessCardLikely: Boolean = false
    ) {
        val hasContactInfo: Boolean
            get() = phones.isNotEmpty() || emails.isNotEmpty()
    }

    private val MYANMAR_DIGITS = charArrayOf('၀', '၁', '၂', '၃', '၄', '၅', '၆', '၇', '၈', '၉')

    private val EMAIL_REGEX = Regex("""\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b""")

    private val WEBSITE_REGEX = Regex(
        """(?i)(?:https?://|www\.)[^\s,;<>"]+|(?<![@\w.])\b[A-Za-z0-9-]{2,}\.(?:com|org|net|io|co|mm|com\.mm|edu|gov|biz|info)(?:/[^\s,;<>"]*)?\b"""
    )

    // Myanmar mobile (+95 9..., 09..., 09 799 123 456, 09-799-123-456)
    private val MYANMAR_MOBILE_REGEX = Regex(
        """(?<=\A|[\s,.:;!?'"()\[\]/|•·\u1000-\u103F\u1040-\u109F-])(?:\(?\+?95\)?[-.\s]*9|\(?09\)?)(?:[-.\s]*\d){7,9}(?=\z|[^\d])"""
    )

    // Labeled phone (Tel:, Ph:, Phone:, Office:, Fax:, Viber:, WhatsApp:, Hotline:, ဖုန်း)
    private val LABELED_PHONE_REGEX = Regex(
        """(?i:(?:tel|telephone|phone|mobile|mob|call|ph|cell|viber|whatsapp|wa|hotline|hot\s*line|office|fax|direct|ext|ဖုန်း|ဆက်သွယ်ရန်)[.:\-\s]+)(\+?(?:\d[-.\s()]*){6,15}\d)(?=\z|[^\d])"""
    )

    // Generic international E.164 (+66..., +65..., +1...)
    private val INTL_PHONE_REGEX = Regex(
        """(?<=\A|[\s,;!?'"()\[\]/|•·])(\+[1-9](?:[-.\s()]?\d){7,14})(?=\z|[^\d])"""
    )

    // Standalone Myanmar landline (e.g. 01-234567, 02-4012345, 067-401234)
    private val LANDLINE_REGEX = Regex(
        """(?<=\A|[\s,.;!?'"()\[\]/|•·])(0[1-8]\d?[-\s]\d{5,8})(?=\z|[^\d])"""
    )

    private val PHONE_OCR_CANDIDATE_TOKEN = Regex("""(?i)\b[Oo]9(?:[-.\s]?[\dOolI]){7,10}\b""")

    private val UI_NOISE_EXACT = setOf(
        "like", "comment", "share", "follow", "following", "followers", "message",
        "send message", "add friend", "requested", "friends", "home", "search",
        "sponsored", "just now", "today", "yesterday", "more", "see more", "see less",
        "call", "video call", "edit", "save", "done", "cancel", "back", "menu",
        "facebook", "messenger", "viber", "whatsapp", "telegram", "all", "posts",
        "photos", "videos", "about", "community", "details", "contact info", "contact"
    )

    private val TITLE_KEYWORDS = listOf(
        "ceo", "cto", "cfo", "coo", "founder", "co-founder", "president", "vice president",
        "managing director", "director", "general manager", "manager", "assistant manager",
        "supervisor", "team lead", "head of", "chief", "executive", "officer",
        "engineer", "architect", "developer", "programmer", "designer", "consultant",
        "coordinator", "specialist", "analyst", "representative", "accountant",
        "lecturer", "professor", "instructor", "advocate", "lawyer",
        "မန်နေဂျာ", "ဒါရိုက်တာ", "အင်ဂျင်နီယာ", "ဥက္ကဋ္ဌ", "အတွင်းရေးမှူး", "တာဝန်ခံ", "ဆရာ", "ဆရာမ", "ဒေါက်တာ"
    )

    private val ORG_KEYWORDS = listOf(
        "co., ltd", "co.,ltd", "co. ltd", "co ltd", "company", "ltd.", "ltd", "limited",
        "inc.", "inc", "corp.", "corporation", "llc", "plc", "group", "enterprise",
        "enterprises", "holding", "holdings", "solutions", "technologies", "technology",
        "tech", "studio", "agency", "services", "trading", "construction", "engineering",
        "clinic", "hospital", "medical center", "pharmacy", "bank", "university",
        "college", "institute", "school", "academy", "foundation", "organization",
        "association", "hotel", "resort", "restaurant", "cafe", "bakery", "store",
        "shop", "mart", "center", "centre", "media", "logistics", "real estate",
        "ကုမ္ပဏီ", "လုပ်ငန်း", "စတိုး", "ဆိုင်", "ဆေးခန်း", "ဆေးရုံ", "ဘဏ်", "ကျောင်း", "အသင်း", "ဖောင်ဒေးရှင်း"
    )

    private val ADDRESS_KEYWORDS = listOf(
        "no.", "no ", "street", "st.", "st ", "road", "rd.", "rd ", "avenue", "ave",
        "lane", "quarter", "ward", "township", "tsp", "district", "floor", "fl.",
        "room", "bldg", "building", "plaza", "tower", "complex", "condo",
        "yangon", "mandalay", "naypyidaw", "nay pyi taw", "taunggyi", "bago", "mawlamyine", "myanmar",
        "singapore", "bangkok", "thailand",
        "အမှတ်", "လမ်း", "ရပ်ကွက်", "မြို့နယ်", "မြို့", "တိုင်း", "ပြည်နယ်", "အခန်း", "အထပ်", "တိုက်",
        "ရန်ကုန်", "မန္တလေး", "နေပြည်တော်"
    )

    fun normalizeDigits(text: String): String {
        val sb = StringBuilder(text.length)
        for (ch in text) {
            val idx = MYANMAR_DIGITS.indexOf(ch)
            if (idx >= 0) sb.append(('0'.code + idx).toChar()) else sb.append(ch)
        }
        return sb.toString()
    }

    internal fun cleanPhoneNumberString(raw: String): String {
        val ascii = normalizeDigits(raw).trim()
        val hasPlus = ascii.startsWith("+")
        val digitsOnly = buildString {
            for (c in ascii) {
                if (c.isDigit()) append(c)
            }
        }
        return if (hasPlus && digitsOnly.isNotEmpty()) "+$digitsOnly" else digitsOnly
    }

    internal fun phoneComparisonKey(phone: String): String {
        val clean = cleanPhoneNumberString(phone).removePrefix("+")
        return when {
            clean.startsWith("959") && clean.length >= 10 -> clean.substring(2) // -> "9..."
            clean.startsWith("95") && clean.length >= 9 -> clean.substring(2)
            clean.startsWith("09") && clean.length >= 9 -> clean.substring(1) // -> "9..."
            clean.startsWith("0") && clean.length >= 7 -> clean.substring(1)
            else -> clean
        }
    }

    private fun repairCommonPhoneOcrArtifacts(asciiLine: String): String {
        return PHONE_OCR_CANDIDATE_TOKEN.replace(asciiLine) { m ->
            val candidate = m.value
            val fixed = buildString {
                candidate.forEachIndexed { i, c ->
                    when {
                        i == 0 && (c == 'O' || c == 'o') -> append('0')
                        c == 'O' || c == 'o' -> append('0')
                        c == 'l' || c == 'I' -> append('1')
                        else -> append(c)
                    }
                }
            }
            val digitsCount = fixed.count { it.isDigit() }
            if (fixed.startsWith("09") && digitsCount in 9..11) fixed else candidate
        }
    }

    private fun inferPhoneLabel(lineLower: String): PhoneLabel {
        return when {
            lineLower.contains("fax") -> PhoneLabel.FAX
            lineLower.contains("office") || lineLower.contains("work") ||
                lineLower.contains("desk") || lineLower.contains("hotline") ||
                lineLower.contains("company") || lineLower.contains("ရုံး") -> PhoneLabel.WORK
            lineLower.contains("home") || lineLower.contains("အိမ်") -> PhoneLabel.HOME
            else -> PhoneLabel.MOBILE
        }
    }

    fun parse(rawText: String): ParsedContactCandidate {
        if (rawText.isBlank()) {
            return ParsedContactCandidate(displayName = "", phones = emptyList(), rawSourceText = rawText)
        }

        val rawLines = rawText.lines().map { it.trim() }.filter { it.isNotBlank() }
        val phones = mutableListOf<ExtractedPhone>()
        val seenPhoneKeys = mutableSetOf<String>()
        val emails = mutableListOf<String>()
        val seenEmails = mutableSetOf<String>()
        val websites = mutableListOf<String>()
        val addressLines = mutableListOf<String>()
        var organization = ""
        var jobTitle = ""
        var explicitName = ""

        val nonMetadataCandidateLines = mutableListOf<String>()

        fun addPhone(rawMatch: String, label: PhoneLabel) {
            val cleaned = cleanPhoneNumberString(rawMatch)
            val digitCount = cleaned.count { it.isDigit() }
            if (digitCount < 7 || digitCount > 15) return
            val key = phoneComparisonKey(cleaned)
            if (key.isNotEmpty() && seenPhoneKeys.add(key)) {
                phones.add(ExtractedPhone(number = cleaned, rawMatch = rawMatch.trim(), label = label))
            }
        }

        for (origLine in rawLines) {
            val asciiLine = repairCommonPhoneOcrArtifacts(normalizeDigits(origLine))
            val lineLower = origLine.lowercase()

            // 1. Extract Emails
            var lineWithoutEmails = origLine
            for (em in EMAIL_REGEX.findAll(origLine)) {
                val emailVal = em.value.trim()
                if (seenEmails.add(emailVal.lowercase())) {
                    emails.add(emailVal)
                }
                lineWithoutEmails = lineWithoutEmails.replace(emailVal, " ")
            }

            // 2. Extract Websites
            var lineWithoutUrls = lineWithoutEmails
            for (wm in WEBSITE_REGEX.findAll(lineWithoutEmails)) {
                val siteVal = wm.value.trim().trimEnd('.', ',', ';')
                if (siteVal.isNotEmpty() && !siteVal.contains("@")) {
                    if (websites.none { it.equals(siteVal, ignoreCase = true) }) {
                        websites.add(siteVal)
                    }
                    lineWithoutUrls = lineWithoutUrls.replace(wm.value, " ")
                }
            }

            // 3. Extract Phones from this line (preserving multiple numbers on one line!)
            val phonesBeforeLine = phones.size
            val label = inferPhoneLabel(asciiLine.lowercase())
            var strippedOfPhonesAscii = asciiLine
            for (pm in MYANMAR_MOBILE_REGEX.findAll(asciiLine)) {
                addPhone(pm.value, label)
                strippedOfPhonesAscii = strippedOfPhonesAscii.replace(pm.value, " ")
            }
            for (pm in LABELED_PHONE_REGEX.findAll(strippedOfPhonesAscii)) {
                val grp = pm.groups[1]?.value ?: continue
                addPhone(grp, label)
                strippedOfPhonesAscii = strippedOfPhonesAscii.replace(grp, " ")
            }
            for (pm in INTL_PHONE_REGEX.findAll(strippedOfPhonesAscii)) {
                val grp = pm.groups[1]?.value ?: continue
                addPhone(grp, label)
                strippedOfPhonesAscii = strippedOfPhonesAscii.replace(grp, " ")
            }
            for (pm in LANDLINE_REGEX.findAll(strippedOfPhonesAscii)) {
                val grp = pm.groups[1]?.value ?: continue
                addPhone(grp, if (label == PhoneLabel.MOBILE) PhoneLabel.WORK else label)
                strippedOfPhonesAscii = strippedOfPhonesAscii.replace(grp, " ")
            }
            val linePhones = if (phones.size > phonesBeforeLine) phones.subList(phonesBeforeLine, phones.size) else emptyList()

            // 4. Explicit label extraction (Name:, Title:, Company:, Org:, Address:)
            val cleanLine = cleanCandidateLineText(lineWithoutUrls, linePhones)
            if (cleanLine.isBlank()) continue

            val explicitNameMatch = Regex("""^(?:name|contact|full\s*name|အမည်)\s*[:\-]\s*(.+)$""", RegexOption.IGNORE_CASE).find(cleanLine)
            if (explicitNameMatch != null && explicitName.isBlank()) {
                explicitName = sanitizeNameValue(explicitNameMatch.groupValues[1])
                continue
            }
            val explicitOrgMatch = Regex("""^(?:company|organization|org|firm|business|ကုမ္ပဏီ|လုပ်ငန်း)\s*[:\-]\s*(.+)$""", RegexOption.IGNORE_CASE).find(cleanLine)
            if (explicitOrgMatch != null && organization.isBlank()) {
                organization = explicitOrgMatch.groupValues[1].trim()
                continue
            }
            val explicitTitleMatch = Regex("""^(?:title|position|role|designation|ရာထူး)\s*[:\-]\s*(.+)$""", RegexOption.IGNORE_CASE).find(cleanLine)
            if (explicitTitleMatch != null && jobTitle.isBlank()) {
                jobTitle = explicitTitleMatch.groupValues[1].trim()
                continue
            }
            val explicitAddrMatch = Regex("""^(?:address|addr|location|office\s*address|လိပ်စာ)\s*[:\-]\s*(.+)$""", RegexOption.IGNORE_CASE).find(cleanLine)
            if (explicitAddrMatch != null) {
                val addrVal = explicitAddrMatch.groupValues[1].trim()
                if (addrVal.isNotEmpty()) addressLines.add(addrVal)
                continue
            }

            if (isUiNoiseLine(cleanLine)) continue

            // 5. Semantic line categorization for Business Cards
            val cleanLower = cleanLine.lowercase()
            when {
                isAddressLine(cleanLower) -> addressLines.add(cleanLine)
                organization.isBlank() && isOrganizationLine(cleanLower) -> organization = cleanLine
                jobTitle.isBlank() && isJobTitleLine(cleanLower) -> jobTitle = cleanLine
                else -> nonMetadataCandidateLines.add(cleanLine)
            }
        }

        val resolvedName = when {
            explicitName.isNotBlank() -> explicitName
            nonMetadataCandidateLines.isNotEmpty() -> {
                // Pick the strongest name candidate (prefer earlier header lines with alphabetic/Myanmar words)
                nonMetadataCandidateLines.firstOrNull { isPlausibleNameLine(it) }
                    ?: organization.takeIf { it.isNotBlank() }
                    ?: ""
            }
            organization.isNotBlank() -> organization
            else -> ""
        }

        // If resolvedName accidentally equals organization, avoid repeating org if we picked it only as fallback
        val finalOrg = if (organization.equals(resolvedName, ignoreCase = true) && explicitName.isBlank() && nonMetadataCandidateLines.isEmpty()) {
            ""
        } else {
            organization
        }

        val isLikelyCard = phones.isNotEmpty() || emails.isNotEmpty()
        val combinedAddress = addressLines.distinct().joinToString(", ")

        return ParsedContactCandidate(
            displayName = resolvedName,
            phones = phones,
            emails = emails,
            organization = finalOrg,
            jobTitle = jobTitle,
            address = combinedAddress,
            website = websites.firstOrNull() ?: "",
            notes = "",
            rawSourceText = rawText,
            isBusinessCardLikely = isLikelyCard
        )
    }

    private fun cleanCandidateLineText(
        lineWithoutUrls: String,
        linePhones: List<ExtractedPhone>
    ): String {
        var working = lineWithoutUrls
        // Remove extracted phone substrings (both exact ASCII matches and Burmese/OCR-digit phone runs)
        for (p in linePhones) {
            if (p.rawMatch.isNotEmpty()) {
                working = working.replace(p.rawMatch, " ")
            }
        }
        if (linePhones.isNotEmpty()) {
            working = working.replace(
                Regex("""(?i)[+]?[0-9\u1040-\u1049Oo][-.\s()/0-9\u1040-\u1049OolI]{5,}[0-9\u1040-\u1049]"""),
                " "
            )
        }
        // Strip leading phone/email/web labels left over after extraction
        working = working.replace(
            Regex("""(?i)\b(?:tel|telephone|phone|mobile|mob|call|ph|cell|viber|whatsapp|wa|email|e-mail|mail|web|website|fax|hotline|office|direct|ext)\s*[:.\-]*"""),
            " "
        )
        working = working.replace(Regex("""(?:ဖုန်း|အီးမေးလ်|ဆက်သွယ်ရန်)\s*[:.\-]*"""), " ")
        working = working.replace(Regex("""[|•·☎📞📱📧✉🌐]+"""), " ")
        working = working.replace(Regex("""\s+"""), " ").trim()
        return sanitizeNameValue(working)
    }

    private fun sanitizeNameValue(input: String): String {
        return input
            .trim { it.isWhitespace() || it in ":-•|/,\"'" }
            .trimStart('.')
            .replace(Regex("""\s+"""), " ")
            .trim { it.isWhitespace() || it in ":-•|/,\"'" }
    }

    private fun isUiNoiseLine(line: String): Boolean {
        val lower = line.lowercase().trim()
        if (lower.length < 2) return true
        if (lower in UI_NOISE_EXACT) return true
        // Status bar / timestamps like "8:21 AM", "2h", "5 mins", "85%", "4G", "5G"
        if (Regex("""^\d{1,2}:\d{2}(\s*(am|pm))?$""", RegexOption.IGNORE_CASE).matches(lower)) return true
        if (Regex("""^\d+\s*(h|hr|hrs|m|min|mins|d|days|w|wks|%)\b.*$""", RegexOption.IGNORE_CASE).matches(lower)) return true
        if (Regex("""^(?:4g|5g|lte|wifi|volte|\d+(\.\d+)?\s*(kb/s|mb/s|kbps))+$""", RegexOption.IGNORE_CASE).matches(lower)) return true
        // Pure punctuation or digits without meaningful letters
        val hasLetter = line.any { it.isLetter() || it in '\u1000'..'\u109F' }
        if (!hasLetter) return true
        // Multiple UI buttons concatenated on one line ("Like Comment Share")
        val words = lower.split(Regex("\\s+")).filter { it.isNotEmpty() }
        if (words.isNotEmpty() && words.all { it in UI_NOISE_EXACT }) return true
        return false
    }

    private fun isJobTitleLine(lineLower: String): Boolean {
        return TITLE_KEYWORDS.any { kw ->
            if (kw.any { it in '\u1000'..'\u109F' }) {
                lineLower.contains(kw)
            } else {
                Regex("""\b${Regex.escape(kw)}\b""").containsMatchIn(lineLower)
            }
        }
    }

    private fun isOrganizationLine(lineLower: String): Boolean {
        return ORG_KEYWORDS.any { kw ->
            if (kw.any { it in '\u1000'..'\u109F' }) {
                lineLower.contains(kw)
            } else {
                Regex("""\b${Regex.escape(kw.trimEnd('.'))}\b""").containsMatchIn(lineLower)
            }
        }
    }

    private fun isAddressLine(lineLower: String): Boolean {
        val hits = ADDRESS_KEYWORDS.count { kw ->
            if (kw.any { it in '\u1000'..'\u109F' }) {
                lineLower.contains(kw)
            } else {
                Regex("""\b${Regex.escape(kw.trimEnd('.', ' '))}\b""").containsMatchIn(lineLower)
            }
        }
        return hits >= 2 || (hits >= 1 && lineLower.length > 18 && lineLower.any { it.isDigit() })
    }

    private fun isPlausibleNameLine(line: String): Boolean {
        if (line.length < 2 || line.length > 64) return false
        val letterCount = line.count { it.isLetter() || it in '\u1000'..'\u109F' }
        val digitCount = line.count { it.isDigit() }
        return letterCount >= 2 && digitCount <= 3
    }

    /**
     * Parses inline natural-language or slash-command contact requests such as
     * "add contact Ko Aung 09799123456 09420987654" or "Ko Kyaw Zin 09799123456 ကို contact ထဲထည့်ပေးပါ".
     */
    fun parseNaturalContactCommand(rawCommandInput: String): ParsedContactCandidate {
        var stripped = rawCommandInput
            .replace(Regex("""(?i)\b(?:please|kindly|can\s+you)\b"""), " ")
            .replace(
                Regex("""(?i)\b(?:add|save|create|store|update|merge|insert|put)\s+(?:a\s+|this\s+|as\s+)?(?:new\s+|existing\s+)?contacts?\b"""),
                " "
            )
            .replace(Regex("""(?i)\b(?:to|into|in|as)\s+(?:my\s+|the\s+|a\s+)?(?:new\s+|existing\s+)?(?:contacts?|phonebook|contact\s+list)\b"""), " ")
            .replace(Regex("""(?i)\b(?:new|existing)\s+contacts?\b"""), " ")
            .replace(Regex("""(?i)\b(?:contact\s+list|phone\s*book)\b"""), " ")
            .replace(Regex("""(?i)^\s*(?:add|save|update|contact)\b[:\s]*"""), " ")
            .replace(Regex("""(?:contact\s*list\s*ထဲ(?:သို့|မှာ)?|contact\s*ထဲ(?:သို့|မှာ)?|အဆက်အသွယ်\s*ထဲ(?:သို့|မှာ)?)"""), " ")
            .replace(Regex("""(?:အသစ်\s*အဖြစ်|ရှိပြီးသား\s*ထဲ(?:သို့|မှာ)?|ပေါင်းထည့်ပေးပါ|ထည့်ပေးပါ|ထည့်ချင်တာ|ထည့်ချင်တယ်|သိမ်းပေးပါ|သိမ်းချင်တာ|ပြင်ပေးပါ|လုပ်ပေးပါ|ထည့်ပါ|သိမ်းပါ|ပြင်ပါ)"""), " ")
            .replace(Regex("""(?<=\A|\s|[0-9\u1040-\u1049])(?:ကို|အဖြစ်|ပေးပါ|ပါ)(?=\s|\z)"""), " ")
            .replace(Regex("""\s+"""), " ")
            .trim()
        if (stripped.isEmpty()) stripped = rawCommandInput
        return parse(stripped)
    }
}
