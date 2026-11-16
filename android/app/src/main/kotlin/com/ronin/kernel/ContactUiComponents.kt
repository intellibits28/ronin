package com.ronin.kernel

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.ronin.kernel.ContactCardParser.ExtractedPhone
import com.ronin.kernel.ContactCardParser.ParsedContactCandidate
import com.ronin.kernel.ContactCardParser.PhoneLabel
import com.ronin.kernel.ContactManager.ExistingContactRecord
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

enum class ContactSaveMode {
    CREATE_NEW,
    UPDATE_EXISTING
}

data class ContactReviewRequest(
    val sourceMessageId: Long?,
    val initialCandidate: ParsedContactCandidate,
    val initialMatches: List<ExistingContactRecord>,
    val initialMode: ContactSaveMode,
    val alreadyBiometricVerified: Boolean
)

/**
 * Displays Privacy Shield reveal/lock controls and detected Business Card / Contact Card actions
 * directly beneath local OCR chat output.
 */
@Composable
fun OcrContactActionCard(
    msg: ChatMessage,
    privacyShieldEnabled: Boolean,
    onUnlockPiiClick: () -> Unit,
    onLockPiiClick: () -> Unit,
    onReviewContactClick: (ContactSaveMode) -> Unit,
    onOpenSavedContactClick: (Long, String?) -> Unit
) {
    val candidate = msg.detectedContact
    val hasRedactedPii = msg.rawOcrText.isNotBlank() && privacyShieldEnabled && PrivacyShield.isSensitive(msg.rawOcrText)

    Column(modifier = Modifier.fillMaxWidth().padding(top = 10.dp)) {
        // Privacy Shield PII Lock/Reveal Toggle for local OCR inspection
        if (hasRedactedPii) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                if (!msg.isOcrPiiUnlocked) {
                    OutlinedButton(
                        onClick = onUnlockPiiClick,
                        border = BorderStroke(1.dp, RoninThemeTokens.WarningAmber.copy(alpha = 0.65f)),
                        colors = ButtonDefaults.outlinedButtonColors(
                            backgroundColor = RoninThemeTokens.WarningAmber.copy(alpha = 0.10f),
                            contentColor = RoninThemeTokens.WarningAmber
                        ),
                        shape = RoninThemeTokens.RadiusSm,
                        contentPadding = PaddingValues(horizontal = 10.dp, vertical = 6.dp)
                    ) {
                        Icon(Icons.Default.Fingerprint, contentDescription = "Unlock", modifier = Modifier.size(15.dp))
                        Spacer(Modifier.width(6.dp))
                        Text(
                            "UNLOCK ORIGINAL TEXT",
                            fontSize = 10.sp,
                            fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.Bold
                        )
                    }
                } else {
                    OutlinedButton(
                        onClick = onLockPiiClick,
                        border = BorderStroke(1.dp, RoninThemeTokens.Border),
                        colors = ButtonDefaults.outlinedButtonColors(
                            backgroundColor = RoninThemeTokens.SurfaceLight,
                            contentColor = RoninThemeTokens.TextSecondary
                        ),
                        shape = RoninThemeTokens.RadiusSm,
                        contentPadding = PaddingValues(horizontal = 10.dp, vertical = 6.dp)
                    ) {
                        Icon(Icons.Default.VisibilityOff, contentDescription = "Mask", modifier = Modifier.size(15.dp))
                        Spacer(Modifier.width(6.dp))
                        Text(
                            "RE-MASK PII",
                            fontSize = 10.sp,
                            fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }
        }

        // Contact / Business Card Action Card
        if (candidate != null && candidate.hasContactInfo) {
            val isUnlocked = msg.isOcrPiiUnlocked || !privacyShieldEnabled
            val topMatch = msg.matchedExistingContacts.firstOrNull()

            Surface(
                color = RoninThemeTokens.SurfaceLight,
                shape = RoninThemeTokens.RadiusMd,
                border = BorderStroke(1.dp, RoninThemeTokens.PrimaryEmerald.copy(alpha = 0.55f)),
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                Icons.Default.ContactPhone,
                                contentDescription = "Contact Card",
                                tint = RoninThemeTokens.PrimaryEmerald,
                                modifier = Modifier.size(16.dp)
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                if (candidate.organization.isNotBlank() || candidate.jobTitle.isNotBlank() || candidate.emails.isNotEmpty()) {
                                    "BUSINESS CARD DETECTED"
                                } else {
                                    "CONTACT INFO DETECTED"
                                },
                                style = RoninThemeTokens.WhisperLabelStyle.copy(color = RoninThemeTokens.PrimaryEmerald)
                            )
                        }
                        Text(
                            "${candidate.phones.size} TEL" + if (candidate.emails.isNotEmpty()) " • ${candidate.emails.size} EMAIL" else "",
                            fontSize = 10.sp,
                            fontFamily = FontFamily.Monospace,
                            color = RoninThemeTokens.TextSecondary
                        )
                    }

                    Spacer(Modifier.height(8.dp))
                    Text(
                        text = candidate.displayName.ifBlank { "Unnamed Contact (Tap to name/edit)" },
                        color = if (candidate.displayName.isBlank()) RoninThemeTokens.WarningAmber else RoninThemeTokens.TextPrimary,
                        fontWeight = FontWeight.Bold,
                        fontSize = 15.sp
                    )

                    if (candidate.jobTitle.isNotBlank() || candidate.organization.isNotBlank()) {
                        val sub = listOf(candidate.jobTitle, candidate.organization).filter { it.isNotBlank() }.joinToString(" • ")
                        Text(sub, color = RoninThemeTokens.TextSecondary, fontSize = 12.sp)
                    }

                    Spacer(Modifier.height(6.dp))
                    candidate.phones.forEachIndexed { index, phone ->
                        val shownNum = if (isUnlocked) phone.number else PrivacyShield.maskPhoneForPreview(phone.number)
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier.padding(vertical = 2.dp)
                        ) {
                            Icon(Icons.Default.Phone, null, tint = Color(0xFF4DD0E1), modifier = Modifier.size(13.dp))
                            Spacer(Modifier.width(6.dp))
                            Text(
                                text = shownNum,
                                color = RoninThemeTokens.TextPrimary,
                                fontFamily = FontFamily.Monospace,
                                fontSize = 13.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                text = "[${phone.label.displayLabel.uppercase()}]",
                                color = RoninThemeTokens.TextMuted,
                                fontSize = 10.sp,
                                fontFamily = FontFamily.Monospace
                            )
                            if (!isUnlocked && index == 0) {
                                Spacer(Modifier.width(6.dp))
                                Icon(Icons.Default.Lock, null, tint = RoninThemeTokens.WarningAmber, modifier = Modifier.size(11.dp))
                            }
                        }
                    }

                    candidate.emails.forEach { email ->
                        val shownEmail = if (isUnlocked) email else PrivacyShield.maskEmailForPreview(email)
                        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(vertical = 2.dp)) {
                            Icon(Icons.Default.Email, null, tint = Color(0xFFCE93D8), modifier = Modifier.size(13.dp))
                            Spacer(Modifier.width(6.dp))
                            Text(shownEmail, color = RoninThemeTokens.TextSecondary, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                        }
                    }

                    if (candidate.address.isNotBlank()) {
                        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 2.dp)) {
                            Icon(Icons.Default.LocationOn, null, tint = RoninThemeTokens.TextSecondary, modifier = Modifier.size(13.dp))
                            Spacer(Modifier.width(6.dp))
                            Text(candidate.address, color = RoninThemeTokens.TextSecondary, fontSize = 11.sp, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        }
                    }

                    if (topMatch != null) {
                        Spacer(Modifier.height(8.dp))
                        Surface(
                            color = Color(0xFF00B8D4).copy(alpha = 0.12f),
                            shape = RoninThemeTokens.RadiusSm,
                            border = BorderStroke(1.dp, Color(0xFF00B8D4).copy(alpha = 0.45f)),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 8.dp, vertical = 6.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(Icons.Default.PersonSearch, null, tint = Color(0xFF4DD0E1), modifier = Modifier.size(15.dp))
                                Spacer(Modifier.width(6.dp))
                                Column {
                                    Text(
                                        "EXISTING MATCH: ${topMatch.displayName}",
                                        color = Color(0xFF4DD0E1),
                                        fontSize = 11.sp,
                                        fontWeight = FontWeight.Bold,
                                        fontFamily = FontFamily.Monospace
                                    )
                                    if (topMatch.matchReason.isNotBlank()) {
                                        Text(
                                            topMatch.matchReason,
                                            color = RoninThemeTokens.TextSecondary,
                                            fontSize = 10.sp
                                        )
                                    }
                                }
                            }
                        }
                    }

                    if (!msg.contactSavedStatus.isNullOrBlank()) {
                        Spacer(Modifier.height(8.dp))
                        Surface(
                            color = RoninThemeTokens.PrimaryEmeraldDim,
                            shape = RoninThemeTokens.RadiusSm,
                            border = BorderStroke(1.dp, RoninThemeTokens.PrimaryEmerald.copy(alpha = 0.6f)),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 10.dp, vertical = 7.dp),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(
                                    msg.contactSavedStatus ?: "",
                                    color = RoninThemeTokens.PrimaryEmerald,
                                    fontSize = 11.sp,
                                    fontWeight = FontWeight.Bold,
                                    modifier = Modifier.weight(1f)
                                )
                                if (msg.savedContactId > 0L) {
                                    TextButton(
                                        onClick = { onOpenSavedContactClick(msg.savedContactId, msg.savedContactLookupKey) },
                                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 2.dp)
                                    ) {
                                        Text("OPEN", color = RoninThemeTokens.PrimaryEmerald, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                                    }
                                }
                            }
                        }
                    }

                    Spacer(Modifier.height(10.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Button(
                            onClick = { onReviewContactClick(ContactSaveMode.CREATE_NEW) },
                            colors = ButtonDefaults.buttonColors(
                                backgroundColor = if (topMatch == null) RoninThemeTokens.PrimaryEmerald else Color(0xFF25283D)
                            ),
                            shape = RoninThemeTokens.RadiusSm,
                            modifier = Modifier.weight(1f).heightIn(min = 40.dp)
                        ) {
                            Icon(
                                Icons.Default.PersonAdd,
                                contentDescription = "New Contact",
                                tint = if (topMatch == null) Color.Black else RoninThemeTokens.PrimaryEmerald,
                                modifier = Modifier.size(15.dp)
                            )
                            Spacer(Modifier.width(5.dp))
                            Text(
                                "New Contact",
                                color = if (topMatch == null) Color.Black else RoninThemeTokens.PrimaryEmerald,
                                fontSize = 11.sp,
                                fontWeight = FontWeight.Bold
                            )
                        }

                        Button(
                            onClick = { onReviewContactClick(ContactSaveMode.UPDATE_EXISTING) },
                            colors = ButtonDefaults.buttonColors(
                                backgroundColor = if (topMatch != null) Color(0xFF4DD0E1) else Color(0xFF25283D)
                            ),
                            shape = RoninThemeTokens.RadiusSm,
                            modifier = Modifier.weight(1f).heightIn(min = 40.dp)
                        ) {
                            Icon(
                                Icons.Default.Sync,
                                contentDescription = "Update Existing",
                                tint = if (topMatch != null) Color.Black else Color(0xFF4DD0E1),
                                modifier = Modifier.size(15.dp)
                            )
                            Spacer(Modifier.width(5.dp))
                            Text(
                                "Update Existing",
                                color = if (topMatch != null) Color.Black else Color(0xFF4DD0E1),
                                fontSize = 11.sp,
                                fontWeight = FontWeight.Bold
                            )
                        }
                    }
                }
            }
        }
    }
}

/**
 * Interactive Contact Review & HITL Approval Dialog.
 * Allows editing OCR-parsed Name, Phone Numbers, Email, Org/Title, Address,
 * choosing between New Contact vs Update Existing (with live contact search + diff preview),
 * and triggering Biometric + WRITE_CONTACTS approval upon confirmation.
 */
@Composable
fun ContactReviewAndApproveDialog(
    request: ContactReviewRequest,
    requireBiometricConfirmation: Boolean,
    onDismiss: () -> Unit,
    onApprove: (
        mode: ContactSaveMode,
        editedCandidate: ParsedContactCandidate,
        targetExisting: ExistingContactRecord?,
        alreadyBiometricVerified: Boolean
    ) -> Unit
) {
    val context = LocalContext.current

    var selectedMode by remember(request) { mutableStateOf(request.initialMode) }
    var displayName by remember(request) { mutableStateOf(request.initialCandidate.displayName) }
    var emailInput by remember(request) { mutableStateOf(request.initialCandidate.emails.joinToString(", ")) }
    var organization by remember(request) { mutableStateOf(request.initialCandidate.organization) }
    var jobTitle by remember(request) { mutableStateOf(request.initialCandidate.jobTitle) }
    var address by remember(request) { mutableStateOf(request.initialCandidate.address) }
    var website by remember(request) { mutableStateOf(request.initialCandidate.website) }
    var notes by remember(request) { mutableStateOf(request.initialCandidate.notes) }

    val phoneEntries = remember(request) {
        mutableStateListOf<ExtractedPhone>().apply {
            if (request.initialCandidate.phones.isNotEmpty()) {
                addAll(request.initialCandidate.phones)
            } else {
                add(ExtractedPhone(number = "", rawMatch = "", label = PhoneLabel.MOBILE))
            }
        }
    }

    var showExtraBusinessFields by remember(request) {
        mutableStateOf(
            request.initialCandidate.organization.isNotBlank() ||
                request.initialCandidate.jobTitle.isNotBlank() ||
                request.initialCandidate.address.isNotBlank() ||
                request.initialCandidate.website.isNotBlank()
        )
    }

    // Existing Contact Search & Selection state for UPDATE_EXISTING mode
    var contactSearchQuery by remember(request) { mutableStateOf("") }
    var searchResults by remember(request) { mutableStateOf(request.initialMatches) }
    var selectedExisting by remember(request) { mutableStateOf(request.initialMatches.firstOrNull()) }

    LaunchedEffect(selectedMode, contactSearchQuery) {
        if (selectedMode == ContactSaveMode.UPDATE_EXISTING) {
            val loaded = withContext(Dispatchers.IO) {
                if (contactSearchQuery.isBlank() && request.initialMatches.isNotEmpty()) {
                    request.initialMatches
                } else {
                    ContactManager.searchContacts(context, contactSearchQuery, limit = 25)
                }
            }
            searchResults = loaded
            if (selectedExisting == null && loaded.isNotEmpty()) {
                selectedExisting = loaded.first()
            }
        }
    }

    val currentCandidate = remember(
        displayName,
        phoneEntries.toList(),
        emailInput,
        organization,
        jobTitle,
        address,
        website,
        notes
    ) {
        ParsedContactCandidate(
            displayName = displayName.trim(),
            phones = phoneEntries.filter { it.number.isNotBlank() }.map { it.copy(number = it.number.trim()) },
            emails = emailInput.split(",").map { it.trim() }.filter { it.isNotEmpty() },
            organization = organization.trim(),
            jobTitle = jobTitle.trim(),
            address = address.trim(),
            website = website.trim(),
            notes = notes.trim(),
            rawSourceText = request.initialCandidate.rawSourceText,
            isBusinessCardLikely = request.initialCandidate.isBusinessCardLikely
        )
    }

    val needsBiometricOnSubmit = !request.alreadyBiometricVerified &&
        (requireBiometricConfirmation || selectedMode == ContactSaveMode.UPDATE_EXISTING)

    val textFieldColors = TextFieldDefaults.outlinedTextFieldColors(
        backgroundColor = RoninThemeTokens.SurfaceLight,
        textColor = RoninThemeTokens.TextPrimary,
        focusedBorderColor = RoninThemeTokens.PrimaryEmerald,
        unfocusedBorderColor = RoninThemeTokens.Border,
        focusedLabelColor = RoninThemeTokens.PrimaryEmerald,
        unfocusedLabelColor = RoninThemeTokens.TextSecondary
    )

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            color = RoninThemeTokens.Surface,
            shape = RoninThemeTokens.RadiusLg,
            border = BorderStroke(1.dp, RoninThemeTokens.PrimaryEmerald.copy(alpha = 0.6f)),
            modifier = Modifier.fillMaxWidth().heightIn(max = 640.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp).fillMaxWidth()) {
                // Header
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.ContactPhone, null, tint = RoninThemeTokens.PrimaryEmerald, modifier = Modifier.size(20.dp))
                        Spacer(Modifier.width(8.dp))
                        Text(
                            "Approve Contact Action",
                            color = RoninThemeTokens.TextPrimary,
                            fontWeight = FontWeight.Bold,
                            fontSize = 16.sp
                        )
                    }
                    IconButton(onClick = onDismiss, modifier = Modifier.size(28.dp)) {
                        Icon(Icons.Default.Close, "Close", tint = RoninThemeTokens.TextSecondary)
                    }
                }

                Spacer(Modifier.height(10.dp))

                // Mode Switcher: New Contact vs Update Existing
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    val isCreate = selectedMode == ContactSaveMode.CREATE_NEW
                    Surface(
                        color = if (isCreate) RoninThemeTokens.PrimaryEmeraldDim else RoninThemeTokens.SurfaceLight,
                        shape = RoninThemeTokens.RadiusSm,
                        border = BorderStroke(1.dp, if (isCreate) RoninThemeTokens.PrimaryEmerald else RoninThemeTokens.Border),
                        modifier = Modifier.weight(1f).clickable { selectedMode = ContactSaveMode.CREATE_NEW }
                    ) {
                        Row(
                            modifier = Modifier.padding(vertical = 8.dp, horizontal = 8.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.Center
                        ) {
                            Icon(
                                Icons.Default.PersonAdd,
                                null,
                                tint = if (isCreate) RoninThemeTokens.PrimaryEmerald else RoninThemeTokens.TextSecondary,
                                modifier = Modifier.size(15.dp)
                            )
                            Spacer(Modifier.width(4.dp))
                            Text(
                                "New Contact",
                                color = if (isCreate) RoninThemeTokens.PrimaryEmerald else RoninThemeTokens.TextSecondary,
                                fontSize = 12.sp,
                                fontWeight = FontWeight.Bold
                            )
                        }
                    }

                    val isUpdate = selectedMode == ContactSaveMode.UPDATE_EXISTING
                    Surface(
                        color = if (isUpdate) Color(0xFF4DD0E1).copy(alpha = 0.16f) else RoninThemeTokens.SurfaceLight,
                        shape = RoninThemeTokens.RadiusSm,
                        border = BorderStroke(1.dp, if (isUpdate) Color(0xFF4DD0E1) else RoninThemeTokens.Border),
                        modifier = Modifier.weight(1f).clickable { selectedMode = ContactSaveMode.UPDATE_EXISTING }
                    ) {
                        Row(
                            modifier = Modifier.padding(vertical = 8.dp, horizontal = 8.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.Center
                        ) {
                            Icon(
                                Icons.Default.Sync,
                                null,
                                tint = if (isUpdate) Color(0xFF4DD0E1) else RoninThemeTokens.TextSecondary,
                                modifier = Modifier.size(15.dp)
                            )
                            Spacer(Modifier.width(4.dp))
                            Text(
                                "Update Existing",
                                color = if (isUpdate) Color(0xFF4DD0E1) else RoninThemeTokens.TextSecondary,
                                fontSize = 12.sp,
                                fontWeight = FontWeight.Bold
                            )
                        }
                    }
                }

                Spacer(Modifier.height(10.dp))

                // Scrollable Body
                Column(
                    modifier = Modifier
                        .weight(1f, fill = false)
                        .verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    if (selectedMode == ContactSaveMode.UPDATE_EXISTING) {
                        Text(
                            "SELECT TARGET CONTACT TO UPDATE",
                            style = RoninThemeTokens.WhisperLabelStyle.copy(color = Color(0xFF4DD0E1))
                        )
                        OutlinedTextField(
                            value = contactSearchQuery,
                            onValueChange = { contactSearchQuery = it },
                            label = { Text("Search contacts...", fontSize = 12.sp) },
                            leadingIcon = { Icon(Icons.Default.Search, null, tint = RoninThemeTokens.TextSecondary, modifier = Modifier.size(18.dp)) },
                            singleLine = true,
                            colors = textFieldColors,
                            modifier = Modifier.fillMaxWidth()
                        )

                        if (searchResults.isEmpty()) {
                            Text(
                                if (!ContactManager.hasReadPermission(context)) "READ_CONTACTS permission required to search existing contacts."
                                else "No matching existing contacts found. Try typing a name/number or switch to New Contact.",
                                color = RoninThemeTokens.WarningAmber,
                                fontSize = 11.sp
                            )
                        } else {
                            Column(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .heightIn(max = 140.dp)
                                    .verticalScroll(rememberScrollState()),
                                verticalArrangement = Arrangement.spacedBy(4.dp)
                            ) {
                                searchResults.forEach { rec ->
                                    val isSelected = selectedExisting?.contactId == rec.contactId
                                    Surface(
                                        color = if (isSelected) Color(0xFF4DD0E1).copy(alpha = 0.16f) else RoninThemeTokens.SurfaceLight,
                                        shape = RoninThemeTokens.RadiusSm,
                                        border = BorderStroke(1.dp, if (isSelected) Color(0xFF4DD0E1) else RoninThemeTokens.Border),
                                        modifier = Modifier.fillMaxWidth().clickable { selectedExisting = rec }
                                    ) {
                                        Row(
                                            modifier = Modifier.padding(8.dp),
                                            verticalAlignment = Alignment.CenterVertically
                                        ) {
                                            RadioButton(
                                                selected = isSelected,
                                                onClick = { selectedExisting = rec },
                                                colors = RadioButtonDefaults.colors(selectedColor = Color(0xFF4DD0E1)),
                                                modifier = Modifier.size(20.dp)
                                            )
                                            Spacer(Modifier.width(8.dp))
                                            Column(modifier = Modifier.weight(1f)) {
                                                Text(rec.displayName.ifBlank { "(No Name)" }, color = RoninThemeTokens.TextPrimary, fontWeight = FontWeight.Bold, fontSize = 12.sp)
                                                if (rec.phones.isNotEmpty()) {
                                                    Text(rec.phones.joinToString(", "), color = RoninThemeTokens.TextSecondary, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                                                }
                                                if (rec.matchReason.isNotBlank()) {
                                                    Text(rec.matchReason, color = RoninThemeTokens.PrimaryEmerald, fontSize = 10.sp)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Merge Diff Summary for selected contact
                        val chosen = selectedExisting
                        if (chosen != null) {
                            val diff = ContactManager.computeUpdateDiff(chosen, currentCandidate)
                            Surface(
                                color = Color(0xFF101820),
                                shape = RoninThemeTokens.RadiusSm,
                                border = BorderStroke(1.dp, RoninThemeTokens.Border),
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Column(modifier = Modifier.padding(8.dp)) {
                                    Text("MERGE PREVIEW FOR '${chosen.displayName}'", style = RoninThemeTokens.WhisperLabelStyle)
                                    Spacer(Modifier.height(3.dp))
                                    if (diff.phonesToAdd.isNotEmpty()) {
                                        Text(
                                            "+ Adding phones: ${diff.phonesToAdd.joinToString { "${it.number} (${it.label.displayLabel})" }}",
                                            color = RoninThemeTokens.PrimaryEmerald,
                                            fontSize = 11.sp
                                        )
                                    }
                                    if (diff.duplicatePhones.isNotEmpty()) {
                                        Text(
                                            "✓ Already saved: ${diff.duplicatePhones.joinToString { it.number }}",
                                            color = RoninThemeTokens.TextMuted,
                                            fontSize = 10.sp
                                        )
                                    }
                                    if (diff.emailsToAdd.isNotEmpty()) {
                                        Text("+ Adding email(s): ${diff.emailsToAdd.joinToString()}", color = RoninThemeTokens.PrimaryEmerald, fontSize = 11.sp)
                                    }
                                    if (diff.updatedOrganization != null) {
                                        Text("+ Organization: ${diff.updatedOrganization}", color = Color(0xFF4DD0E1), fontSize = 11.sp)
                                    }
                                    if (!diff.hasNewChanges) {
                                        Text("No new changes detected yet — edit fields below if needed.", color = RoninThemeTokens.TextSecondary, fontSize = 11.sp)
                                    }
                                }
                            }
                        }
                    }

                    // Name Field
                    OutlinedTextField(
                        value = displayName,
                        onValueChange = { displayName = it },
                        label = { Text(if (selectedMode == ContactSaveMode.UPDATE_EXISTING) "Display Name (leave as-is or edit)" else "Contact Name") },
                        singleLine = true,
                        colors = textFieldColors,
                        modifier = Modifier.fillMaxWidth()
                    )

                    // Phone Numbers List
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("PHONE NUMBERS (${phoneEntries.size})", style = RoninThemeTokens.WhisperLabelStyle)
                        TextButton(
                            onClick = {
                                phoneEntries.add(ExtractedPhone(number = "", rawMatch = "", label = PhoneLabel.MOBILE))
                            },
                            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 2.dp)
                        ) {
                            Icon(Icons.Default.Add, null, tint = RoninThemeTokens.PrimaryEmerald, modifier = Modifier.size(14.dp))
                            Spacer(Modifier.width(4.dp))
                            Text("Add Phone", color = RoninThemeTokens.PrimaryEmerald, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                        }
                    }

                    phoneEntries.forEachIndexed { idx, entry ->
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            OutlinedTextField(
                                value = entry.number,
                                onValueChange = { newVal -> phoneEntries[idx] = entry.copy(number = newVal) },
                                label = { Text("Phone ${idx + 1}") },
                                singleLine = true,
                                colors = textFieldColors,
                                modifier = Modifier.weight(1f)
                            )
                            Surface(
                                color = RoninThemeTokens.SurfaceLight,
                                shape = RoninThemeTokens.RadiusSm,
                                border = BorderStroke(1.dp, RoninThemeTokens.Border),
                                modifier = Modifier.clickable {
                                    val nextOrdinal = (entry.label.ordinal + 1) % PhoneLabel.values().size
                                    phoneEntries[idx] = entry.copy(label = PhoneLabel.values()[nextOrdinal])
                                }
                            ) {
                                Text(
                                    entry.label.displayLabel,
                                    color = Color(0xFF4DD0E1),
                                    fontSize = 10.sp,
                                    fontWeight = FontWeight.Bold,
                                    fontFamily = FontFamily.Monospace,
                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 12.dp)
                                )
                            }
                            if (phoneEntries.size > 1) {
                                IconButton(
                                    onClick = { phoneEntries.removeAt(idx) },
                                    modifier = Modifier.size(32.dp)
                                ) {
                                    Icon(Icons.Default.Delete, "Remove phone", tint = RoninThemeTokens.ErrorCrimson, modifier = Modifier.size(18.dp))
                                }
                            }
                        }
                    }

                    OutlinedTextField(
                        value = emailInput,
                        onValueChange = { emailInput = it },
                        label = { Text("Email Address(es)") },
                        singleLine = true,
                        colors = textFieldColors,
                        modifier = Modifier.fillMaxWidth()
                    )

                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { showExtraBusinessFields = !showExtraBusinessFields }
                            .padding(vertical = 4.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            if (showExtraBusinessFields) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                            null,
                            tint = RoninThemeTokens.TextSecondary,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(Modifier.width(6.dp))
                        Text(
                            "Business Card Details (Company, Title, Address, Website)",
                            color = RoninThemeTokens.TextSecondary,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Medium
                        )
                    }

                    AnimatedVisibility(visible = showExtraBusinessFields) {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedTextField(
                                value = organization,
                                onValueChange = { organization = it },
                                label = { Text("Company / Organization") },
                                singleLine = true,
                                colors = textFieldColors,
                                modifier = Modifier.fillMaxWidth()
                            )
                            OutlinedTextField(
                                value = jobTitle,
                                onValueChange = { jobTitle = it },
                                label = { Text("Job Title / Role") },
                                singleLine = true,
                                colors = textFieldColors,
                                modifier = Modifier.fillMaxWidth()
                            )
                            OutlinedTextField(
                                value = address,
                                onValueChange = { address = it },
                                label = { Text("Address") },
                                colors = textFieldColors,
                                modifier = Modifier.fillMaxWidth()
                            )
                            OutlinedTextField(
                                value = website,
                                onValueChange = { website = it },
                                label = { Text("Website") },
                                singleLine = true,
                                colors = textFieldColors,
                                modifier = Modifier.fillMaxWidth()
                            )
                            OutlinedTextField(
                                value = notes,
                                onValueChange = { notes = it },
                                label = { Text("Notes") },
                                colors = textFieldColors,
                                modifier = Modifier.fillMaxWidth()
                            )
                        }
                    }

                    if (needsBiometricOnSubmit) {
                        Surface(
                            color = RoninThemeTokens.WarningAmber.copy(alpha = 0.10f),
                            shape = RoninThemeTokens.RadiusSm,
                            border = BorderStroke(1.dp, RoninThemeTokens.WarningAmber.copy(alpha = 0.4f)),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Row(
                                modifier = Modifier.padding(8.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(Icons.Default.Fingerprint, null, tint = RoninThemeTokens.WarningAmber, modifier = Modifier.size(16.dp))
                                Spacer(Modifier.width(8.dp))
                                Text(
                                    "Biometric confirmation will be requested before writing to Contacts.",
                                    color = RoninThemeTokens.WarningAmber,
                                    fontSize = 11.sp
                                )
                            }
                        }
                    }
                }

                Spacer(Modifier.height(12.dp))
                Divider(color = RoninThemeTokens.Border)
                Spacer(Modifier.height(10.dp))

                // Pinned Approve / Cancel Action Bar
                val canSubmit = currentCandidate.hasContactInfo &&
                    (selectedMode == ContactSaveMode.CREATE_NEW || selectedExisting != null)

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    OutlinedButton(
                        onClick = onDismiss,
                        border = BorderStroke(1.dp, RoninThemeTokens.Border),
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = RoninThemeTokens.TextSecondary),
                        shape = RoninThemeTokens.RadiusSm,
                        modifier = Modifier.weight(1f).heightIn(min = 44.dp)
                    ) {
                        Text("Cancel", fontWeight = FontWeight.Bold)
                    }

                    Button(
                        onClick = {
                            onApprove(
                                selectedMode,
                                currentCandidate,
                                selectedExisting,
                                request.alreadyBiometricVerified
                            )
                        },
                        enabled = canSubmit,
                        colors = ButtonDefaults.buttonColors(
                            backgroundColor = if (selectedMode == ContactSaveMode.UPDATE_EXISTING) Color(0xFF4DD0E1) else RoninThemeTokens.PrimaryEmerald,
                            disabledBackgroundColor = RoninThemeTokens.Border
                        ),
                        shape = RoninThemeTokens.RadiusSm,
                        modifier = Modifier.weight(1.4f).heightIn(min = 44.dp)
                    ) {
                        if (needsBiometricOnSubmit) {
                            Icon(Icons.Default.Fingerprint, null, tint = Color.Black, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(6.dp))
                        }
                        Text(
                            if (selectedMode == ContactSaveMode.UPDATE_EXISTING) "Approve & Update" else "Approve & Save",
                            color = Color.Black,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }
        }
    }
}
