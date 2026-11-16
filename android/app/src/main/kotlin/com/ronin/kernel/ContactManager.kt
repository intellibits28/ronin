package com.ronin.kernel

import android.Manifest
import android.content.ContentProviderOperation
import android.content.ContentUris
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.provider.ContactsContract
import android.util.Log
import androidx.core.content.ContextCompat
import com.ronin.kernel.ContactCardParser.ExtractedPhone
import com.ronin.kernel.ContactCardParser.ParsedContactCandidate
import com.ronin.kernel.ContactCardParser.PhoneLabel

/**
 * Android ContactsProvider integration + pure-Kotlin contact matching & merge diffing.
 * Supports multi-phone insertion, duplicate-safe merging into existing contacts, and system viewer launch.
 */
object ContactManager {
    private const val TAG = "Ronin_ContactManager"

    data class ExistingContactRecord(
        val contactId: Long,
        val rawContactId: Long = -1L,
        val lookupKey: String = "",
        val displayName: String,
        val phones: List<String> = emptyList(),
        val emails: List<String> = emptyList(),
        val organization: String = "",
        val jobTitle: String = "",
        val matchScore: Int = 0,
        val matchReason: String = ""
    )

    data class ContactUpdateDiff(
        val targetContact: ExistingContactRecord,
        val phonesToAdd: List<ExtractedPhone>,
        val duplicatePhones: List<ExtractedPhone>,
        val emailsToAdd: List<String>,
        val updatedDisplayName: String?,
        val updatedOrganization: String?,
        val updatedJobTitle: String?,
        val updatedAddress: String?,
        val updatedWebsite: String?
    ) {
        val hasNewChanges: Boolean
            get() = phonesToAdd.isNotEmpty() || emailsToAdd.isNotEmpty() ||
                updatedDisplayName != null || updatedOrganization != null ||
                updatedJobTitle != null || updatedAddress != null || updatedWebsite != null
    }

    data class ContactWriteResult(
        val success: Boolean,
        val action: String = "", // "CREATED" or "UPDATED"
        val contactId: Long = -1L,
        val lookupKey: String = "",
        val displayName: String = "",
        val addedPhonesCount: Int = 0,
        val message: String = "",
        val error: String? = null
    )

    fun hasReadPermission(context: Context): Boolean {
        return ContextCompat.checkSelfPermission(context, Manifest.permission.READ_CONTACTS) == PackageManager.PERMISSION_GRANTED
    }

    fun hasWritePermission(context: Context): Boolean {
        return ContextCompat.checkSelfPermission(context, Manifest.permission.WRITE_CONTACTS) == PackageManager.PERMISSION_GRANTED
    }

    fun phonesMatch(phoneA: String, phoneB: String): Boolean {
        val keyA = ContactCardParser.phoneComparisonKey(phoneA)
        val keyB = ContactCardParser.phoneComparisonKey(phoneB)
        if (keyA.length < 6 || keyB.length < 6) return false
        return keyA == keyB
    }

    fun computeNameMatchScore(candidateName: String, existingName: String): Int {
        val a = candidateName.trim().lowercase().replace(Regex("""\s+"""), " ")
        val b = existingName.trim().lowercase().replace(Regex("""\s+"""), " ")
        if (a.length < 2 || b.length < 2) return 0
        if (a == b) return 85
        if (a.length >= 4 && b.length >= 4 && (a.contains(b) || b.contains(a))) return 65
        val tokensA = a.split(" ").filter { it.length >= 2 }.toSet()
        val tokensB = b.split(" ").filter { it.length >= 2 }.toSet()
        if (tokensA.isNotEmpty() && tokensB.isNotEmpty()) {
            val shared = tokensA.intersect(tokensB).size
            if (shared >= 2 || (shared == 1 && (tokensA.size == 1 || tokensB.size == 1))) {
                return 55
            }
        }
        return 0
    }

    fun matchContactsAgainstList(
        candidate: ParsedContactCandidate,
        existingContacts: List<ExistingContactRecord>
    ): List<ExistingContactRecord> {
        val matches = mutableListOf<ExistingContactRecord>()

        for (existing in existingContacts) {
            var bestScore = 0
            val reasons = mutableListOf<String>()

            // 1. Phone match (highest confidence)
            for (candPhone in candidate.phones) {
                val matchedPhone = existing.phones.firstOrNull { phonesMatch(candPhone.number, it) }
                if (matchedPhone != null) {
                    bestScore = maxOf(bestScore, 100)
                    reasons.add("Phone matches ${candPhone.number}")
                    break
                }
            }

            // 2. Email match
            for (candEmail in candidate.emails) {
                if (existing.emails.any { it.equals(candEmail, ignoreCase = true) }) {
                    bestScore = maxOf(bestScore, 95)
                    reasons.add("Email matches $candEmail")
                    break
                }
            }

            // 3. Name match
            if (candidate.displayName.isNotBlank()) {
                val nameScore = computeNameMatchScore(candidate.displayName, existing.displayName)
                if (nameScore > 0) {
                    bestScore = maxOf(bestScore, nameScore)
                    reasons.add(if (nameScore >= 85) "Exact name match" else "Similar name")
                }
            }

            if (bestScore > 0) {
                matches.add(
                    existing.copy(
                        matchScore = bestScore,
                        matchReason = reasons.distinct().joinToString(" • ")
                    )
                )
            }
        }

        return matches.sortedWith(compareByDescending<ExistingContactRecord> { it.matchScore }.thenBy { it.displayName })
    }

    fun computeUpdateDiff(
        existing: ExistingContactRecord,
        candidate: ParsedContactCandidate
    ): ContactUpdateDiff {
        val phonesToAdd = mutableListOf<ExtractedPhone>()
        val duplicatePhones = mutableListOf<ExtractedPhone>()

        for (p in candidate.phones) {
            if (p.number.isBlank()) continue
            val isDup = existing.phones.any { phonesMatch(it, p.number) } ||
                phonesToAdd.any { phonesMatch(it.number, p.number) }
            if (isDup) duplicatePhones.add(p) else phonesToAdd.add(p)
        }

        val emailsToAdd = candidate.emails.filter { candEmail ->
            candEmail.isNotBlank() && existing.emails.none { it.equals(candEmail, ignoreCase = true) }
        }.distinctBy { it.lowercase() }

        val updatedName = candidate.displayName.trim().takeIf {
            it.isNotEmpty() && !it.equals(existing.displayName.trim(), ignoreCase = false)
        }
        val updatedOrg = candidate.organization.trim().takeIf {
            it.isNotEmpty() && !it.equals(existing.organization.trim(), ignoreCase = true)
        }
        val updatedTitle = candidate.jobTitle.trim().takeIf {
            it.isNotEmpty() && !it.equals(existing.jobTitle.trim(), ignoreCase = true)
        }
        val updatedAddress = candidate.address.trim().takeIf { it.isNotEmpty() }
        val updatedWebsite = candidate.website.trim().takeIf { it.isNotEmpty() }

        return ContactUpdateDiff(
            targetContact = existing,
            phonesToAdd = phonesToAdd,
            duplicatePhones = duplicatePhones,
            emailsToAdd = emailsToAdd,
            updatedDisplayName = updatedName,
            updatedOrganization = updatedOrg,
            updatedJobTitle = updatedTitle,
            updatedAddress = updatedAddress,
            updatedWebsite = updatedWebsite
        )
    }

    fun findMatchingContacts(context: Context, candidate: ParsedContactCandidate): List<ExistingContactRecord> {
        if (!hasReadPermission(context)) return emptyList()
        val all = loadAllContactSummaries(context)
        return matchContactsAgainstList(candidate, all).take(8)
    }

    fun searchContacts(context: Context, query: String = "", limit: Int = 40): List<ExistingContactRecord> {
        if (!hasReadPermission(context)) return emptyList()
        val all = loadAllContactSummaries(context)
        val q = query.trim().lowercase()
        val filtered = if (q.isEmpty()) {
            all
        } else {
            val qDigits = ContactCardParser.cleanPhoneNumberString(q)
            all.filter { c ->
                c.displayName.lowercase().contains(q) ||
                    c.organization.lowercase().contains(q) ||
                    c.emails.any { it.lowercase().contains(q) } ||
                    (qDigits.length >= 3 && c.phones.any { ContactCardParser.cleanPhoneNumberString(it).contains(qDigits) })
            }
        }
        return filtered.sortedBy { it.displayName.lowercase() }.take(limit)
    }

    private fun loadAllContactSummaries(context: Context): List<ExistingContactRecord> {
        val resolver = context.contentResolver
        val map = LinkedHashMap<Long, ExistingContactRecord>()
        val phonesByContact = HashMap<Long, MutableList<String>>()
        val emailsByContact = HashMap<Long, MutableList<String>>()
        val orgByContact = HashMap<Long, Pair<String, String>>()

        try {
            // 1. Load contacts base rows
            resolver.query(
                ContactsContract.Contacts.CONTENT_URI,
                arrayOf(
                    ContactsContract.Contacts._ID,
                    ContactsContract.Contacts.DISPLAY_NAME_PRIMARY,
                    ContactsContract.Contacts.LOOKUP_KEY
                ),
                null,
                null,
                null
            )?.use { cursor ->
                val idIdx = cursor.getColumnIndex(ContactsContract.Contacts._ID)
                val nameIdx = cursor.getColumnIndex(ContactsContract.Contacts.DISPLAY_NAME_PRIMARY)
                val lookupIdx = cursor.getColumnIndex(ContactsContract.Contacts.LOOKUP_KEY)
                while (cursor.moveToNext()) {
                    val cid = if (idIdx >= 0) cursor.getLong(idIdx) else continue
                    val name = if (nameIdx >= 0) cursor.getString(nameIdx) ?: "" else ""
                    val lookup = if (lookupIdx >= 0) cursor.getString(lookupIdx) ?: "" else ""
                    map[cid] = ExistingContactRecord(
                        contactId = cid,
                        lookupKey = lookup,
                        displayName = name
                    )
                }
            }

            // 2. Load phone numbers
            resolver.query(
                ContactsContract.CommonDataKinds.Phone.CONTENT_URI,
                arrayOf(
                    ContactsContract.CommonDataKinds.Phone.CONTACT_ID,
                    ContactsContract.CommonDataKinds.Phone.NUMBER
                ),
                null,
                null,
                null
            )?.use { cursor ->
                val cidIdx = cursor.getColumnIndex(ContactsContract.CommonDataKinds.Phone.CONTACT_ID)
                val numIdx = cursor.getColumnIndex(ContactsContract.CommonDataKinds.Phone.NUMBER)
                while (cursor.moveToNext()) {
                    val cid = if (cidIdx >= 0) cursor.getLong(cidIdx) else continue
                    val num = if (numIdx >= 0) cursor.getString(numIdx) ?: "" else ""
                    if (num.isNotBlank()) {
                        phonesByContact.getOrPut(cid) { mutableListOf() }.add(num)
                    }
                }
            }

            // 3. Load emails
            resolver.query(
                ContactsContract.CommonDataKinds.Email.CONTENT_URI,
                arrayOf(
                    ContactsContract.CommonDataKinds.Email.CONTACT_ID,
                    ContactsContract.CommonDataKinds.Email.ADDRESS
                ),
                null,
                null,
                null
            )?.use { cursor ->
                val cidIdx = cursor.getColumnIndex(ContactsContract.CommonDataKinds.Email.CONTACT_ID)
                val emailIdx = cursor.getColumnIndex(ContactsContract.CommonDataKinds.Email.ADDRESS)
                while (cursor.moveToNext()) {
                    val cid = if (cidIdx >= 0) cursor.getLong(cidIdx) else continue
                    val email = if (emailIdx >= 0) cursor.getString(emailIdx) ?: "" else ""
                    if (email.isNotBlank()) {
                        emailsByContact.getOrPut(cid) { mutableListOf() }.add(email)
                    }
                }
            }

            // 4. Load Organizations
            resolver.query(
                ContactsContract.Data.CONTENT_URI,
                arrayOf(
                    ContactsContract.Data.CONTACT_ID,
                    ContactsContract.CommonDataKinds.Organization.COMPANY,
                    ContactsContract.CommonDataKinds.Organization.TITLE
                ),
                "${ContactsContract.Data.MIMETYPE} = ?",
                arrayOf(ContactsContract.CommonDataKinds.Organization.CONTENT_ITEM_TYPE),
                null
            )?.use { cursor ->
                val cidIdx = cursor.getColumnIndex(ContactsContract.Data.CONTACT_ID)
                val compIdx = cursor.getColumnIndex(ContactsContract.CommonDataKinds.Organization.COMPANY)
                val titleIdx = cursor.getColumnIndex(ContactsContract.CommonDataKinds.Organization.TITLE)
                while (cursor.moveToNext()) {
                    val cid = if (cidIdx >= 0) cursor.getLong(cidIdx) else continue
                    val comp = if (compIdx >= 0) cursor.getString(compIdx) ?: "" else ""
                    val title = if (titleIdx >= 0) cursor.getString(titleIdx) ?: "" else ""
                    if (comp.isNotBlank() || title.isNotBlank()) {
                        orgByContact[cid] = comp to title
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error loading contact summaries: ${e.message}", e)
        }

        return map.values.map { rec ->
            val orgPair = orgByContact[rec.contactId]
            rec.copy(
                phones = phonesByContact[rec.contactId]?.distinct() ?: emptyList(),
                emails = emailsByContact[rec.contactId]?.distinct() ?: emptyList(),
                organization = orgPair?.first ?: "",
                jobTitle = orgPair?.second ?: ""
            )
        }
    }

    private fun mapPhoneType(label: PhoneLabel): Int {
        return when (label) {
            PhoneLabel.MOBILE -> ContactsContract.CommonDataKinds.Phone.TYPE_MOBILE
            PhoneLabel.WORK -> ContactsContract.CommonDataKinds.Phone.TYPE_WORK
            PhoneLabel.HOME -> ContactsContract.CommonDataKinds.Phone.TYPE_HOME
            PhoneLabel.FAX -> ContactsContract.CommonDataKinds.Phone.TYPE_FAX_WORK
            PhoneLabel.OTHER -> ContactsContract.CommonDataKinds.Phone.TYPE_OTHER
        }
    }

    fun insertNewContact(context: Context, candidate: ParsedContactCandidate): ContactWriteResult {
        if (!hasWritePermission(context)) {
            return ContactWriteResult(success = false, error = "WRITE_CONTACTS permission is required.")
        }

        val finalName = candidate.displayName.trim().ifBlank {
            candidate.organization.trim().ifBlank {
                candidate.phones.firstOrNull()?.number ?: candidate.emails.firstOrNull() ?: "New Contact"
            }
        }

        val ops = ArrayList<ContentProviderOperation>()
        ops.add(
            ContentProviderOperation.newInsert(ContactsContract.RawContacts.CONTENT_URI)
                .withValue(ContactsContract.RawContacts.ACCOUNT_TYPE, null)
                .withValue(ContactsContract.RawContacts.ACCOUNT_NAME, null)
                .build()
        )

        // Name
        ops.add(
            ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                .withValueBackReference(ContactsContract.Data.RAW_CONTACT_ID, 0)
                .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.StructuredName.CONTENT_ITEM_TYPE)
                .withValue(ContactsContract.CommonDataKinds.StructuredName.DISPLAY_NAME, finalName)
                .build()
        )

        // Phones
        val cleanPhones = candidate.phones.filter { it.number.isNotBlank() }
        cleanPhones.forEachIndexed { idx, phone ->
            ops.add(
                ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                    .withValueBackReference(ContactsContract.Data.RAW_CONTACT_ID, 0)
                    .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.Phone.CONTENT_ITEM_TYPE)
                    .withValue(ContactsContract.CommonDataKinds.Phone.NUMBER, phone.number.trim())
                    .withValue(ContactsContract.CommonDataKinds.Phone.TYPE, mapPhoneType(phone.label))
                    .apply { if (idx == 0) withValue(ContactsContract.Data.IS_PRIMARY, 1) }
                    .build()
            )
        }

        // Emails
        val cleanEmails = candidate.emails.filter { it.isNotBlank() }.distinct()
        cleanEmails.forEach { email ->
            ops.add(
                ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                    .withValueBackReference(ContactsContract.Data.RAW_CONTACT_ID, 0)
                    .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.Email.CONTENT_ITEM_TYPE)
                    .withValue(ContactsContract.CommonDataKinds.Email.ADDRESS, email.trim())
                    .withValue(ContactsContract.CommonDataKinds.Email.TYPE, ContactsContract.CommonDataKinds.Email.TYPE_WORK)
                    .build()
            )
        }

        // Organization & Title
        if (candidate.organization.isNotBlank() || candidate.jobTitle.isNotBlank()) {
            ops.add(
                ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                    .withValueBackReference(ContactsContract.Data.RAW_CONTACT_ID, 0)
                    .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.Organization.CONTENT_ITEM_TYPE)
                    .withValue(ContactsContract.CommonDataKinds.Organization.COMPANY, candidate.organization.trim())
                    .withValue(ContactsContract.CommonDataKinds.Organization.TITLE, candidate.jobTitle.trim())
                    .withValue(ContactsContract.CommonDataKinds.Organization.TYPE, ContactsContract.CommonDataKinds.Organization.TYPE_WORK)
                    .build()
            )
        }

        // Address
        if (candidate.address.isNotBlank()) {
            ops.add(
                ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                    .withValueBackReference(ContactsContract.Data.RAW_CONTACT_ID, 0)
                    .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.StructuredPostal.CONTENT_ITEM_TYPE)
                    .withValue(ContactsContract.CommonDataKinds.StructuredPostal.FORMATTED_ADDRESS, candidate.address.trim())
                    .withValue(ContactsContract.CommonDataKinds.StructuredPostal.TYPE, ContactsContract.CommonDataKinds.StructuredPostal.TYPE_WORK)
                    .build()
            )
        }

        // Website
        if (candidate.website.isNotBlank()) {
            ops.add(
                ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                    .withValueBackReference(ContactsContract.Data.RAW_CONTACT_ID, 0)
                    .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.Website.CONTENT_ITEM_TYPE)
                    .withValue(ContactsContract.CommonDataKinds.Website.URL, candidate.website.trim())
                    .withValue(ContactsContract.CommonDataKinds.Website.TYPE, ContactsContract.CommonDataKinds.Website.TYPE_WORK)
                    .build()
            )
        }

        // Note
        if (candidate.notes.isNotBlank()) {
            ops.add(
                ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                    .withValueBackReference(ContactsContract.Data.RAW_CONTACT_ID, 0)
                    .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.Note.CONTENT_ITEM_TYPE)
                    .withValue(ContactsContract.CommonDataKinds.Note.NOTE, candidate.notes.trim())
                    .build()
            )
        }

        return try {
            val results = context.contentResolver.applyBatch(ContactsContract.AUTHORITY, ops)
            val rawUri = results.firstOrNull()?.uri
            val rawId = rawUri?.let { ContentUris.parseId(it) } ?: -1L
            var contactId = -1L
            var lookupKey = ""
            if (rawId > 0) {
                context.contentResolver.query(
                    ContactsContract.RawContacts.CONTENT_URI,
                    arrayOf(ContactsContract.RawContacts.CONTACT_ID),
                    "${ContactsContract.RawContacts._ID} = ?",
                    arrayOf(rawId.toString()),
                    null
                )?.use { cur ->
                    if (cur.moveToFirst()) contactId = cur.getLong(0)
                }
                if (contactId > 0) {
                    context.contentResolver.query(
                        ContactsContract.Contacts.CONTENT_URI,
                        arrayOf(ContactsContract.Contacts.LOOKUP_KEY),
                        "${ContactsContract.Contacts._ID} = ?",
                        arrayOf(contactId.toString()),
                        null
                    )?.use { cur ->
                        if (cur.moveToFirst()) lookupKey = cur.getString(0) ?: ""
                    }
                }
            }

            ContactWriteResult(
                success = true,
                action = "CREATED",
                contactId = contactId,
                lookupKey = lookupKey,
                displayName = finalName,
                addedPhonesCount = cleanPhones.size,
                message = "Saved new contact '$finalName' with ${cleanPhones.size} phone number(s)."
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to insert contact: ${e.message}", e)
            ContactWriteResult(success = false, error = "Failed to create contact: ${e.message}")
        }
    }

    fun updateExistingContact(
        context: Context,
        existingContact: ExistingContactRecord,
        candidate: ParsedContactCandidate
    ): ContactWriteResult {
        if (!hasReadPermission(context) || !hasWritePermission(context)) {
            return ContactWriteResult(success = false, error = "READ_CONTACTS and WRITE_CONTACTS permissions are required.")
        }

        val rawId = findPrimaryRawContactId(context, existingContact.contactId)
        if (rawId <= 0L) {
            return ContactWriteResult(success = false, error = "Could not locate writable RawContact for '${existingContact.displayName}'.")
        }

        val diff = computeUpdateDiff(existingContact, candidate)
        val ops = ArrayList<ContentProviderOperation>()

        // 1. Add non-duplicate phone numbers
        diff.phonesToAdd.forEach { phone ->
            ops.add(
                ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                    .withValue(ContactsContract.Data.RAW_CONTACT_ID, rawId)
                    .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.Phone.CONTENT_ITEM_TYPE)
                    .withValue(ContactsContract.CommonDataKinds.Phone.NUMBER, phone.number.trim())
                    .withValue(ContactsContract.CommonDataKinds.Phone.TYPE, mapPhoneType(phone.label))
                    .build()
            )
        }

        // 2. Add non-duplicate emails
        diff.emailsToAdd.forEach { email ->
            ops.add(
                ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                    .withValue(ContactsContract.Data.RAW_CONTACT_ID, rawId)
                    .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.Email.CONTENT_ITEM_TYPE)
                    .withValue(ContactsContract.CommonDataKinds.Email.ADDRESS, email.trim())
                    .withValue(ContactsContract.CommonDataKinds.Email.TYPE, ContactsContract.CommonDataKinds.Email.TYPE_WORK)
                    .build()
            )
        }

        // 3. Update Display Name if changed cleanly
        if (diff.updatedDisplayName != null) {
            val nameDataId = findDataRowId(context, rawId, ContactsContract.CommonDataKinds.StructuredName.CONTENT_ITEM_TYPE)
            if (nameDataId > 0L) {
                ops.add(
                    ContentProviderOperation.newUpdate(ContactsContract.Data.CONTENT_URI)
                        .withSelection("${ContactsContract.Data._ID} = ?", arrayOf(nameDataId.toString()))
                        .withValue(ContactsContract.CommonDataKinds.StructuredName.DISPLAY_NAME, diff.updatedDisplayName)
                        .build()
                )
            } else {
                ops.add(
                    ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                        .withValue(ContactsContract.Data.RAW_CONTACT_ID, rawId)
                        .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.StructuredName.CONTENT_ITEM_TYPE)
                        .withValue(ContactsContract.CommonDataKinds.StructuredName.DISPLAY_NAME, diff.updatedDisplayName)
                        .build()
                )
            }
        }

        // 4. Update or insert Organization / Title
        if (diff.updatedOrganization != null || diff.updatedJobTitle != null) {
            val orgDataId = findDataRowId(context, rawId, ContactsContract.CommonDataKinds.Organization.CONTENT_ITEM_TYPE)
            val compVal = diff.updatedOrganization ?: existingContact.organization
            val titleVal = diff.updatedJobTitle ?: existingContact.jobTitle
            if (orgDataId > 0L) {
                ops.add(
                    ContentProviderOperation.newUpdate(ContactsContract.Data.CONTENT_URI)
                        .withSelection("${ContactsContract.Data._ID} = ?", arrayOf(orgDataId.toString()))
                        .withValue(ContactsContract.CommonDataKinds.Organization.COMPANY, compVal)
                        .withValue(ContactsContract.CommonDataKinds.Organization.TITLE, titleVal)
                        .build()
                )
            } else {
                ops.add(
                    ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                        .withValue(ContactsContract.Data.RAW_CONTACT_ID, rawId)
                        .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.Organization.CONTENT_ITEM_TYPE)
                        .withValue(ContactsContract.CommonDataKinds.Organization.COMPANY, compVal)
                        .withValue(ContactsContract.CommonDataKinds.Organization.TITLE, titleVal)
                        .withValue(ContactsContract.CommonDataKinds.Organization.TYPE, ContactsContract.CommonDataKinds.Organization.TYPE_WORK)
                        .build()
                )
            }
        }

        // 5. Add/Update Address if provided
        if (diff.updatedAddress != null) {
            val addrDataId = findDataRowId(context, rawId, ContactsContract.CommonDataKinds.StructuredPostal.CONTENT_ITEM_TYPE)
            if (addrDataId > 0L) {
                ops.add(
                    ContentProviderOperation.newUpdate(ContactsContract.Data.CONTENT_URI)
                        .withSelection("${ContactsContract.Data._ID} = ?", arrayOf(addrDataId.toString()))
                        .withValue(ContactsContract.CommonDataKinds.StructuredPostal.FORMATTED_ADDRESS, diff.updatedAddress)
                        .build()
                )
            } else {
                ops.add(
                    ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                        .withValue(ContactsContract.Data.RAW_CONTACT_ID, rawId)
                        .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.StructuredPostal.CONTENT_ITEM_TYPE)
                        .withValue(ContactsContract.CommonDataKinds.StructuredPostal.FORMATTED_ADDRESS, diff.updatedAddress)
                        .withValue(ContactsContract.CommonDataKinds.StructuredPostal.TYPE, ContactsContract.CommonDataKinds.StructuredPostal.TYPE_WORK)
                        .build()
                )
            }
        }

        // 6. Add Website if provided
        if (diff.updatedWebsite != null) {
            val webDataId = findDataRowId(context, rawId, ContactsContract.CommonDataKinds.Website.CONTENT_ITEM_TYPE)
            if (webDataId > 0L) {
                ops.add(
                    ContentProviderOperation.newUpdate(ContactsContract.Data.CONTENT_URI)
                        .withSelection("${ContactsContract.Data._ID} = ?", arrayOf(webDataId.toString()))
                        .withValue(ContactsContract.CommonDataKinds.Website.URL, diff.updatedWebsite)
                        .build()
                )
            } else {
                ops.add(
                    ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                        .withValue(ContactsContract.Data.RAW_CONTACT_ID, rawId)
                        .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.Website.CONTENT_ITEM_TYPE)
                        .withValue(ContactsContract.CommonDataKinds.Website.URL, diff.updatedWebsite)
                        .build()
                )
            }
        }

        // 7. Add Notes if provided
        if (candidate.notes.isNotBlank()) {
            ops.add(
                ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                    .withValue(ContactsContract.Data.RAW_CONTACT_ID, rawId)
                    .withValue(ContactsContract.Data.MIMETYPE, ContactsContract.CommonDataKinds.Note.CONTENT_ITEM_TYPE)
                    .withValue(ContactsContract.CommonDataKinds.Note.NOTE, candidate.notes.trim())
                    .build()
            )
        }

        val finalDisplayName = diff.updatedDisplayName ?: existingContact.displayName
        if (ops.isEmpty()) {
            return ContactWriteResult(
                success = true,
                action = "UPDATED",
                contactId = existingContact.contactId,
                lookupKey = existingContact.lookupKey,
                displayName = finalDisplayName,
                addedPhonesCount = 0,
                message = "'$finalDisplayName' already contains all detected details (no changes needed)."
            )
        }

        return try {
            context.contentResolver.applyBatch(ContactsContract.AUTHORITY, ops)
            ContactWriteResult(
                success = true,
                action = "UPDATED",
                contactId = existingContact.contactId,
                lookupKey = existingContact.lookupKey,
                displayName = finalDisplayName,
                addedPhonesCount = diff.phonesToAdd.size,
                message = "Updated '${finalDisplayName}' (+${diff.phonesToAdd.size} new phone(s), +${diff.emailsToAdd.size} email(s))."
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to update contact: ${e.message}", e)
            ContactWriteResult(success = false, error = "Failed to update contact: ${e.message}")
        }
    }

    private fun findPrimaryRawContactId(context: Context, contactId: Long): Long {
        var fallbackId = -1L
        context.contentResolver.query(
            ContactsContract.RawContacts.CONTENT_URI,
            arrayOf(ContactsContract.RawContacts._ID, ContactsContract.RawContacts.ACCOUNT_TYPE),
            "${ContactsContract.RawContacts.CONTACT_ID} = ? AND ${ContactsContract.RawContacts.DELETED} = 0",
            arrayOf(contactId.toString()),
            null
        )?.use { cursor ->
            while (cursor.moveToNext()) {
                val id = cursor.getLong(0)
                val accType = cursor.getString(1) ?: ""
                if (fallbackId < 0) fallbackId = id
                // Prefer local or Google writable accounts over read-only social/WhatsApp raw contacts
                if (accType.isEmpty() || accType.contains("google", true) || accType.contains("phone", true) || accType.contains("local", true)) {
                    return id
                }
            }
        }
        return fallbackId
    }

    private fun findDataRowId(context: Context, rawContactId: Long, mimeType: String): Long {
        context.contentResolver.query(
            ContactsContract.Data.CONTENT_URI,
            arrayOf(ContactsContract.Data._ID),
            "${ContactsContract.Data.RAW_CONTACT_ID} = ? AND ${ContactsContract.Data.MIMETYPE} = ?",
            arrayOf(rawContactId.toString(), mimeType),
            null
        )?.use { cursor ->
            if (cursor.moveToFirst()) return cursor.getLong(0)
        }
        return -1L
    }

    fun openContactInSystemApp(context: Context, contactId: Long, lookupKey: String? = null) {
        try {
            val uri = if (contactId > 0 && !lookupKey.isNullOrBlank()) {
                ContactsContract.Contacts.getLookupUri(contactId, lookupKey)
            } else if (contactId > 0) {
                ContentUris.withAppendedId(ContactsContract.Contacts.CONTENT_URI, contactId)
            } else {
                ContactsContract.Contacts.CONTENT_URI
            }
            val intent = Intent(Intent.ACTION_VIEW, uri).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
        } catch (e: Exception) {
            Log.e(TAG, "Could not open system contact: ${e.message}", e)
        }
    }
}
