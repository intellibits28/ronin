package com.ronin.kernel

import android.content.Context
import android.widget.Toast
import androidx.compose.animation.*
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File
import kotlinx.coroutines.launch
import com.ronin.kernel.shm.*

@Composable
fun SystemStatusCard(chatViewModel: ChatViewModel) {
    AnimatedVisibility(
        visible = chatViewModel.showSysInfo,
        enter = expandVertically() + fadeIn(),
        exit = shrinkVertically() + fadeOut()
    ) {
        Surface(
            color = Color(0xFF161922),
            shape = RoundedCornerShape(12.dp),
            border = BorderStroke(1.dp, Color(0xFF2D3142)),
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)
        ) {
            Column(modifier = Modifier.padding(14.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Ronin Kernel", fontWeight = FontWeight.Bold, color = Color.White, fontSize = 14.sp)
                        Spacer(Modifier.width(8.dp))
                        val statusColor = when {
                            chatViewModel.kernelStatus.contains("Ready", true) || chatViewModel.kernelStatus.contains("Active", true) -> Color(0xFF66BB6A)
                            chatViewModel.kernelStatus.contains("Error", true) -> Color(0xFFEF5350)
                            else -> Color(0xFFFFCA28)
                        }
                        Text(
                            text = if (chatViewModel.kernelStatus.contains("Ready", true)) "🟢 Ready" else "🟡 ${chatViewModel.kernelStatus}",
                            color = statusColor,
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Medium
                        )
                    }
                    Text(
                        text = "v3.0 Production",
                        color = Color.Gray,
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace
                    )
                }
                Spacer(Modifier.height(12.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    val tempColor = if (chatViewModel.systemTemperature > 40f) Color(0xFFEF5350) else Color(0xFF66BB6A)
                    SystemMetricItem("Thermal", "${chatViewModel.systemTemperature}°C", tempColor)
                    SystemMetricItem("RAM", "${"%.2f".format(chatViewModel.ramUsedGB)}GB / ${"%.1f".format(chatViewModel.ramTotalGB)}GB", Color.White)
                    SystemMetricItem("LMK Pressure", "${chatViewModel.lmkPressure}%", if (chatViewModel.lmkPressure > 50) Color(0xFFFFCA28) else Color(0xFF64B5F6))
                }
                Spacer(Modifier.height(10.dp))
                Divider(color = Color.DarkGray.copy(alpha = 0.5f))
                Spacer(Modifier.height(10.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Column {
                        Text("Cloud Provider", fontSize = 10.sp, color = Color.Gray)
                        Text("${chatViewModel.primaryCloudProvider} (${chatViewModel.apiConnectionStatus})", fontSize = 12.sp, color = if (chatViewModel.apiConnectionStatus == "Connected") Color(0xFF66BB6A) else Color.Cyan, fontWeight = FontWeight.Medium)
                    }
                    Column(horizontalAlignment = Alignment.End) {
                        Text("Local Brain", fontSize = 10.sp, color = Color.Gray)
                        Text(if (chatViewModel.isGemmaReady) "Active" else "Standby", fontSize = 12.sp, color = if (chatViewModel.isGemmaReady) Color(0xFF66BB6A) else Color.Gray, fontWeight = FontWeight.Medium)
                    }
                }
            }
        }
    }
}

@Composable
fun SystemMetricItem(label: String, value: String, valueColor: Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, fontSize = 10.sp, color = Color.Gray)
        Spacer(Modifier.height(2.dp))
        Text(value, fontSize = 13.sp, color = valueColor, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
    }
}

@Composable
fun ReasoningConsole(chatViewModel: ChatViewModel) {
    val context = LocalContext.current
    val scrollState = rememberScrollState()

    LaunchedEffect(chatViewModel.reasoningLogsText) {
        if (chatViewModel.showReasoning) {
            scrollState.animateScrollTo(scrollState.maxValue)
        }
    }

    Column(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)
    ) {
        Surface(
            color = Color(0xFF12141C),
            shape = RoundedCornerShape(8.dp),
            border = BorderStroke(1.dp, Color(0xFF64B5F6).copy(alpha = 0.4f)),
            modifier = Modifier.fillMaxWidth().clickable { chatViewModel.showReasoning = !chatViewModel.showReasoning }
        ) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = if (chatViewModel.showReasoning) "▼ Reasoning Console" else "🧠 Reasoning Console",
                        color = Color.White,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(Modifier.width(8.dp))
                    if (!chatViewModel.showReasoning) {
                        Text("Tap to expand", color = Color.Gray, fontSize = 10.sp, fontStyle = androidx.compose.ui.text.font.FontStyle.Italic)
                    }
                }
                if (chatViewModel.showReasoning) {
                    IconButton(
                        onClick = {
                            val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                            val clip = android.content.ClipData.newPlainText("Ronin Console Logs", chatViewModel.reasoningLogsText)
                            clipboard.setPrimaryClip(clip)
                            Toast.makeText(context, "Console logs copied to clipboard", Toast.LENGTH_SHORT).show()
                        },
                        modifier = Modifier.size(24.dp)
                    ) {
                        Icon(Icons.Default.ContentCopy, "Copy", tint = Color.Cyan, modifier = Modifier.size(14.dp))
                    }
                }
            }
        }

        AnimatedVisibility(
            visible = chatViewModel.showReasoning,
            enter = expandVertically() + fadeIn(),
            exit = shrinkVertically() + fadeOut()
        ) {
            Surface(
                color = Color(0xFF0C0E14),
                shape = RoundedCornerShape(bottomStart = 8.dp, bottomEnd = 8.dp),
                border = BorderStroke(1.dp, Color(0xFF64B5F6).copy(alpha = 0.2f)),
                modifier = Modifier.fillMaxWidth().heightIn(min = 80.dp, max = 220.dp)
            ) {
                SelectionContainer {
                    Text(
                        text = if (chatViewModel.reasoningLogsText.isEmpty()) "> No reasoning traces recorded yet..." else chatViewModel.reasoningLogsText,
                        color = Color(0xFF80DEEA),
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        lineHeight = 14.sp,
                        modifier = Modifier.verticalScroll(scrollState).padding(10.dp)
                    )
                }
            }
        }
    }
}

@Composable
fun DeveloperHud(chatViewModel: ChatViewModel) {
    if (!chatViewModel.showDevHUD) return

    Surface(
        color = Color(0xEE10121C),
        shape = RoundedCornerShape(10.dp),
        border = BorderStroke(1.5.dp, Color(0xFF64B5F6)),
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("COG HUD (1Hz)", color = Color(0xFFFFD54F), fontSize = 11.sp, fontWeight = FontWeight.Bold)
                Text(if (chatViewModel.apiLatencyMs > 0) "LATENCY: ${chatViewModel.apiLatencyMs} ms" else "LATENCY: -- ms", color = Color.Cyan, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
            Spacer(Modifier.height(6.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("STATE: ${chatViewModel.hudState}", color = Color.White, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                Text("CONF: ${"%.2f".format(chatViewModel.hudConfidence)}", color = if (chatViewModel.hudConfidence > 0.5f) Color(0xFF66BB6A) else Color(0xFFEF5350), fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
            }
            Spacer(Modifier.height(2.dp))
            Text("INTENT: ${chatViewModel.hudIntent}", color = Color.White, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
            if (chatViewModel.hudPlan.isNotEmpty()) {
                Spacer(Modifier.height(4.dp))
                Text("PLAN: ${chatViewModel.hudPlan}", color = Color(0xFF80DEEA), fontSize = 11.sp, fontFamily = FontFamily.Monospace)
            }

            if (chatViewModel.sensorFreqHz > 0) {
                Spacer(Modifier.height(8.dp))
                Divider(color = Color.DarkGray)
                Spacer(Modifier.height(6.dp))
                Text("DSP SENSOR DATA", color = Color(0xFFFFA726), fontSize = 10.sp, fontWeight = FontWeight.Bold)
                val anomalyColor = if (chatViewModel.sensorAnomaly) Color(0xFFEF5350) else Color(0xFF66BB6A)
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("PEAK: ${"%.1f".format(chatViewModel.sensorFreqHz)}Hz", color = anomalyColor, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                    Text("PSD: ${"%.1f".format(chatViewModel.sensorPsdDb)}dB", color = anomalyColor, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                }
            }

            val tuner = chatViewModel.tunerResult
            if (tuner.status != "IDLE") {
                Spacer(Modifier.height(8.dp))
                Divider(color = Color.DarkGray)
                Spacer(Modifier.height(6.dp))
                Text("🎸 GUITAR TUNER", color = Color(0xFFCE93D8), fontSize = 10.sp, fontWeight = FontWeight.Bold)
                val (tunerColor, tunerIcon) = when (tuner.status) {
                    "IN_TUNE" -> Color(0xFF66BB6A) to "✅"
                    "SHARP" -> Color(0xFFEF5350) to "🔺"
                    "FLAT" -> Color(0xFF42A5F5) to "🔻"
                    else -> Color.Gray to "⏸"
                }
                Text("$tunerIcon ${tuner.nearestString} target", color = tunerColor, fontSize = 12.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("DETECTED: ${"%.2f".format(tuner.detectedHz)}Hz (${tuner.detectedNote})", color = Color.White, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text("TARGET: ${"%.2f".format(tuner.nearestStringHz)}Hz (${tuner.nearestString})", color = Color.Gray, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                }
                val deviationLabel = when {
                    tuner.deviationCents > 0 -> "+${"%.1f".format(tuner.deviationCents)}¢ SHARP"
                    tuner.deviationCents < 0 -> "${"%.1f".format(tuner.deviationCents)}¢ FLAT"
                    else -> "0.0¢ IN TUNE"
                }
                Text(deviationLabel, color = tunerColor, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                val needleProgress = ((tuner.deviationCents.coerceIn(-50.0, 50.0) + 50.0) / 100.0).toFloat()
                Spacer(Modifier.height(4.dp))
                LinearProgressIndicator(
                    progress = needleProgress,
                    modifier = Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)),
                    color = tunerColor,
                    backgroundColor = Color(0xFF37474F)
                )
            }
        }
    }
}

@Composable
fun ShmResultCard(
    status: String,
    healthIndex: String,
    resonanceHz: String,
    noiseDb: String,
    confidence: String,
    activity: MainActivity? = null,
    chatViewModel: ChatViewModel? = null
) {
    val context = LocalContext.current
    var showDetails by remember { mutableStateOf(false) }
    var showExportDialog by remember { mutableStateOf(false) }
    var selectedExportFormat by remember { mutableStateOf(ExportFormat.ENGINEERING_JSON) }
    var showAiReview by remember { mutableStateOf(false) }

    val session by remember(status, healthIndex, resonanceHz, noiseDb, chatViewModel?.sensorCandidates?.toList()) {
        mutableStateOf(
            ShmSession(
                features = ShmFeatures(
                    baselineF0Hz = try { resonanceHz.replace("Hz", "").trim().toFloat() } catch (_: Exception) { 0.0f },
                    filteredF0Hz = try { resonanceHz.replace("Hz", "").trim().toFloat() } catch (_: Exception) { 0.0f },
                    noiseFloorDb = try { noiseDb.replace("dB", "").trim().toFloat() } catch (_: Exception) { 0.0f },
                    confidence = confidence
                ),
                dspResult = DspResult(
                    topCandidates = chatViewModel?.sensorCandidates?.toList() ?: emptyList()
                ),
                decision = ShmDecision(
                    status = status,
                    healthIndex = healthIndex,
                    riskLevel = if (status.equals("HEALTHY", true)) "LOW" else "ELEVATED"
                )
            )
        )
    }

    if (showDetails) {
        ShmDetailScreen(session = session) { showDetails = false }
    }

    if (showExportDialog) {
        ExportOptionDialog(
            initialFormat = selectedExportFormat,
            onConfirm = { options, format ->
                showExportDialog = false
                try {
                    val file = ShmExportManager.exportToFile(context, session, format, options)
                    // Persist to repository
                    activity?.let {
                        val repo = ShmSessionRepository(com.ronin.kernel.DatabaseHelper(it))
                        repo.saveSession(session)
                    }
                    ShmExportManager.shareViaAndroidShareSheet(context, file)
                } catch (e: Exception) {
                    Toast.makeText(context, "Export Error: ${e.message}", Toast.LENGTH_LONG).show()
                }
            },
            onDismiss = { showExportDialog = false }
        )
    }

    if (showAiReview && chatViewModel != null) {
        AIReviewScreen(
            session = session,
            activity = activity,
            chatViewModel = chatViewModel,
            onDismiss = { showAiReview = false }
        )
    }

    Surface(
        color = Color(0xFF1B202D),
        shape = RoundedCornerShape(12.dp),
        border = BorderStroke(1.dp, Color(0xFF64B5F6).copy(alpha = 0.5f)),
        modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween, modifier = Modifier.fillMaxWidth()) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("🏢", fontSize = 18.sp)
                    Spacer(Modifier.width(8.dp))
                    Text("Structural Health", fontWeight = FontWeight.Bold, color = Color.White, fontSize = 15.sp)
                }
                val badgeColor = when {
                    status.equals("HEALTHY", true) -> Color(0xFF66BB6A)
                    status.equals("WARNING", true) -> Color(0xFFFFCA28)
                    else -> Color(0xFFEF5350)
                }
                Surface(color = badgeColor.copy(alpha = 0.2f), shape = RoundedCornerShape(6.dp), border = BorderStroke(1.dp, badgeColor)) {
                    Text(status.uppercase(), color = badgeColor, fontWeight = FontWeight.Bold, fontSize = 11.sp, modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp))
                }
            }
            Spacer(Modifier.height(12.dp))
            Divider(color = Color.DarkGray.copy(alpha = 0.4f))
            Spacer(Modifier.height(12.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                SystemMetricItem("Health Index", healthIndex, Color(0xFF80DEEA))
                SystemMetricItem("Resonance (f₀)", resonanceHz, Color.White)
                SystemMetricItem("Noise Floor", noiseDb, Color.Gray)
                SystemMetricItem("Confidence", confidence, Color(0xFF66BB6A))
            }

            // Action bar (Requirement 4)
            ShmExportActions(
                onViewDetails = { showDetails = true },
                onExportJson = {
                    selectedExportFormat = ExportFormat.ENGINEERING_JSON
                    showExportDialog = true
                },
                onExportReport = {
                    selectedExportFormat = ExportFormat.HUMAN_REPORT
                    showExportDialog = true
                },
                onAnalyzeAi = {
                    if (chatViewModel != null) {
                        showAiReview = true
                    } else {
                        Toast.makeText(context, "AI Review requires active chat session", Toast.LENGTH_SHORT).show()
                    }
                }
            )
        }
    }
}

@Composable
fun CognitiveTraceCard(traceText: String) {
    Surface(
        color = Color(0xFF141722),
        shape = RoundedCornerShape(8.dp),
        border = BorderStroke(1.dp, Color(0xFFBA68C8).copy(alpha = 0.4f)),
        modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp)
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("🧠 Cognitive Trace", color = Color(0xFFCE93D8), fontSize = 11.sp, fontWeight = FontWeight.Bold)
            }
            Spacer(Modifier.height(6.dp))
            SelectionContainer {
                Text(
                    text = traceText.trim(),
                    color = Color(0xFFE1BEE7),
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace,
                    lineHeight = 14.sp
                )
            }
        }
    }
}

@Composable
fun AgentResponseCard(
    msg: ChatMessage,
    chatViewModel: ChatViewModel,
    onContinue: () -> Unit,
    onFeedback: (Boolean) -> Unit
) {
    val isUser = msg.sender == "User"
    val context = LocalContext.current
    var isThoughtExpanded by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
        horizontalAlignment = if (isUser) Alignment.End else Alignment.Start
    ) {
        if (!isUser) {
            Text("Ronin", fontSize = 11.sp, color = Color.Gray, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(start = 6.dp, bottom = 3.dp))
        }
        Surface(
            color = if (isUser) Color(0xFF2D3142) else Color(0xFF1E2130),
            shape = RoundedCornerShape(
                topStart = 16.dp,
                topEnd = 16.dp,
                bottomStart = if (isUser) 16.dp else 4.dp,
                bottomEnd = if (isUser) 4.dp else 16.dp
            ),
            elevation = 3.dp
        ) {
            SelectionContainer {
                Column(modifier = Modifier.padding(14.dp)) {
                    // Cognitive Trace / Reflection handling (Requirement 5)
                    val hasTrace = msg.thoughtContent.isNotEmpty() || msg.content.contains("Reflection:")
                    if (!isUser && hasTrace && chatViewModel.showDevHUD) {
                        val traceContent = if (msg.thoughtContent.isNotEmpty()) msg.thoughtContent else {
                            val idx = msg.content.indexOf("Reflection:")
                            if (idx != -1) msg.content.substring(idx) else ""
                        }
                        if (traceContent.isNotEmpty()) {
                            CognitiveTraceCard(traceContent)
                        }
                    }

                    // Thought expandable section
                    if (!isUser && (msg.thoughtContent.isNotEmpty() || msg.isThinking) && !chatViewModel.showDevHUD) {
                        Row(
                            modifier = Modifier.fillMaxWidth().clickable { isThoughtExpanded = !isThoughtExpanded }.padding(bottom = if (isThoughtExpanded || msg.content.isNotEmpty()) 8.dp else 0.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Icon(if (isThoughtExpanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown, null, tint = Color.Gray, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(4.dp))
                            Text(if (msg.isThinking) "Reasoning..." else "Thought Process", color = Color.Gray, fontSize = 11.sp, fontStyle = androidx.compose.ui.text.font.FontStyle.Italic)
                            if (msg.isThinking) {
                                Spacer(Modifier.width(8.dp))
                                CircularProgressIndicator(modifier = Modifier.size(10.dp), strokeWidth = 1.dp, color = Color.Gray)
                            }
                        }
                        AnimatedVisibility(visible = isThoughtExpanded) {
                            Text(msg.thoughtContent, color = Color.Cyan, fontSize = 11.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.padding(bottom = if (msg.content.isNotEmpty()) 8.dp else 0.dp, start = 8.dp))
                        }
                    }

                    if (!isUser && msg.content.isEmpty() && !msg.isThinking && msg.thoughtContent.isEmpty()) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(modifier = Modifier.size(14.dp), strokeWidth = 2.dp, color = Color.Cyan)
                            Spacer(Modifier.width(8.dp))
                            Text("Ronin is preparing response...", color = Color.Cyan, fontSize = 13.sp, fontFamily = FontFamily.Monospace)
                        }
                    }

                    if (msg.content.isNotEmpty()) {
                        // Check if content is SHM engineering response (Requirement 4)
                        val isShmStructured = !isUser && (msg.content.contains("Structural Health", true) || msg.content.contains("Health Index", true))
                        if (isShmStructured) {
                            // Extract metrics if structured or render directly
                            val status = if (msg.content.contains("HEALTHY", true)) "HEALTHY" else if (msg.content.contains("WARNING", true)) "WARNING" else "NORMAL"
                            ShmResultCard(
                                status = status,
                                healthIndex = chatViewModel.sensorHealthIndex,
                                resonanceHz = if (chatViewModel.sensorFreqHz > 0) "${"%.2f".format(chatViewModel.sensorFreqHz)} Hz" else "0.00 Hz",
                                noiseDb = if (chatViewModel.sensorNoiseFloorDb != 0f) "${"%.1f".format(chatViewModel.sensorNoiseFloorDb)} dB" else "${"%.1f".format(chatViewModel.sensorPsdDb)} dB",
                                confidence = chatViewModel.sensorConfidence,
                                activity = context as? MainActivity,
                                chatViewModel = chatViewModel
                            )
                        } else {
                            Row(verticalAlignment = Alignment.Top) {
                                Text(
                                    text = msg.content,
                                    color = Color.White,
                                    fontSize = 15.sp,
                                    lineHeight = 22.sp,
                                    modifier = Modifier.weight(1f)
                                )
                                IconButton(
                                    onClick = {
                                        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                                        val clip = android.content.ClipData.newPlainText("Ronin Message", msg.content)
                                        clipboard.setPrimaryClip(clip)
                                        Toast.makeText(context, "Copied to clipboard", Toast.LENGTH_SHORT).show()
                                    },
                                    modifier = Modifier.size(24.dp).padding(start = 4.dp, top = 2.dp)
                                ) {
                                    Icon(Icons.Default.ContentCopy, "Copy", tint = Color.Gray, modifier = Modifier.size(14.dp))
                                }
                            }
                        }
                    }

                    if (msg.isTruncated && !isUser) {
                        Spacer(Modifier.height(8.dp))
                        OutlinedButton(onClick = onContinue, modifier = Modifier.fillMaxWidth()) {
                            Text("Continue Generation", color = Color(0xFF64B5F6), fontSize = 12.sp)
                        }
                    }

                    // RLHF Feedback Row
                    if (!isUser && msg.content.isNotEmpty() && !msg.isThinking && !msg.feedbackGiven) {
                        Spacer(Modifier.height(12.dp))
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("Helpful?", color = Color.Gray, fontSize = 11.sp)
                            Spacer(Modifier.width(8.dp))
                            IconButton(onClick = { msg.feedbackGiven = true; onFeedback(true) }, modifier = Modifier.size(28.dp)) {
                                Icon(Icons.Default.ThumbUp, null, tint = Color.Gray.copy(alpha = 0.7f), modifier = Modifier.size(16.dp))
                            }
                            Spacer(Modifier.width(4.dp))
                            IconButton(onClick = { msg.feedbackGiven = true; onFeedback(false) }, modifier = Modifier.size(28.dp)) {
                                Icon(Icons.Default.ThumbDown, null, tint = Color.Gray.copy(alpha = 0.7f), modifier = Modifier.size(16.dp))
                            }
                        }
                    } else if (!isUser && msg.feedbackGiven) {
                        Spacer(Modifier.height(8.dp))
                        Text("Thanks for your feedback!", color = Color(0xFF64B5F6), fontSize = 10.sp, fontStyle = androidx.compose.ui.text.font.FontStyle.Italic)
                    }
                }
            }
        }
    }
}

@Composable
fun InputBar(
    currentInput: String,
    onInputChange: (String) -> Unit,
    isGenerating: Boolean,
    onSendOrStop: () -> Unit,
    onSuggestionClick: (String) -> Unit,
    showCommandSuggestions: Boolean
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        if (showCommandSuggestions) {
            val suggestions = listOf("/status", "/skills", "/model", "/reset", "/history").filter { it.startsWith(currentInput.lowercase()) }
            if (suggestions.isNotEmpty()) {
                Surface(
                    color = Color(0xFF25283D),
                    shape = RoundedCornerShape(topStart = 12.dp, topEnd = 12.dp),
                    elevation = 8.dp,
                    modifier = Modifier.padding(horizontal = 16.dp).fillMaxWidth(0.85f)
                ) {
                    LazyColumn(modifier = Modifier.heightIn(max = 180.dp)) {
                        items(suggestions) { s ->
                            TextButton(
                                onClick = { onSuggestionClick("$s ") },
                                modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)
                            ) {
                                Text(s, color = Color.White, modifier = Modifier.fillMaxWidth(), textAlign = TextAlign.Start)
                            }
                        }
                    }
                }
            }
        }
        Surface(
            elevation = 12.dp,
            color = Color(0xFF1A1C2C)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                TextField(
                    value = currentInput,
                    onValueChange = onInputChange,
                    placeholder = { Text("Ask Ronin kernel anything...", color = Color.Gray, fontSize = 14.sp) },
                    modifier = Modifier.weight(1f).clip(RoundedCornerShape(24.dp)).heightIn(min = 48.dp),
                    colors = TextFieldDefaults.textFieldColors(
                        backgroundColor = Color(0xFF25283D),
                        textColor = Color.White,
                        focusedIndicatorColor = Color.Transparent,
                        unfocusedIndicatorColor = Color.Transparent
                    ),
                    textStyle = androidx.compose.ui.text.TextStyle(fontSize = 14.sp)
                )
                Spacer(Modifier.width(8.dp))
                Surface(
                    color = if (isGenerating) Color(0xFFEF5350).copy(alpha = 0.2f) else Color(0xFF64B5F6).copy(alpha = 0.2f),
                    shape = RoundedCornerShape(24.dp),
                    modifier = Modifier.size(48.dp)
                ) {
                    IconButton(onClick = onSendOrStop, modifier = Modifier.fillMaxSize()) {
                        Icon(
                            imageVector = if (isGenerating) Icons.Default.Stop else Icons.Default.Send,
                            contentDescription = if (isGenerating) "Stop" else "Send",
                            tint = if (isGenerating) Color(0xFFEF5350) else Color(0xFF64B5F6),
                            modifier = Modifier.size(24.dp)
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun SettingsSection(
    chatViewModel: ChatViewModel,
    activity: MainActivity?,
    brainPicker: androidx.activity.result.ActivityResultLauncher<Array<String>>,
    onOpenModelPicker: () -> Unit,
    onTestConnection: () -> Unit
) {
    LazyColumn(
        modifier = Modifier.fillMaxSize().background(Color(0xFF161824)).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            Text("Ronin Configuration", fontSize = 20.sp, fontWeight = FontWeight.Bold, color = Color.White)
            Text("Production AI Agent Operating System", fontSize = 12.sp, color = Color.Gray)
        }

        // Section 1: Runtime
        item {
            SettingsCardSection(title = "1. Runtime") {
                SettingsSwitchRow("Cloud Only Mode", chatViewModel.cloudOnlyMode) {
                    chatViewModel.cloudOnlyMode = it
                    activity?.saveCloudOnlyMode(it)
                }
                SettingsSwitchRow("Reasoning Logs", chatViewModel.showReasoning) {
                    chatViewModel.showReasoning = it
                }
                SettingsSwitchRow("Developer HUD (1Hz)", chatViewModel.showDevHUD) {
                    chatViewModel.showDevHUD = it
                }
            }
        }

        // Section 2: Generation
        item {
            SettingsCardSection(title = "2. Generation") {
                SettingsSliderRow("Temperature", "${"%.2f".format(chatViewModel.samplingTemperature)}", chatViewModel.samplingTemperature, 0.1f..1.5f) { valV ->
                    chatViewModel.samplingTemperature = valV
                    activity?.saveSamplingParams(chatViewModel.samplingTemperature, chatViewModel.topK, chatViewModel.topP)
                }
                SettingsSliderRow("Top-K", "${chatViewModel.topK}", chatViewModel.topK.toFloat(), 1f..100f) { valV ->
                    chatViewModel.topK = valV.toInt()
                    activity?.saveSamplingParams(chatViewModel.samplingTemperature, chatViewModel.topK, chatViewModel.topP)
                }
                SettingsSliderRow("Top-P", "${"%.2f".format(chatViewModel.topP)}", chatViewModel.topP, 0.1f..1.0f) { valV ->
                    chatViewModel.topP = valV
                    activity?.saveSamplingParams(chatViewModel.samplingTemperature, chatViewModel.topK, chatViewModel.topP)
                }
                SettingsSliderRow("Max Tokens", "${chatViewModel.maxTokens}", chatViewModel.maxTokens.toFloat(), 512f..4096f) { valV ->
                    chatViewModel.maxTokens = valV.toInt()
                    activity?.saveMaxTokens(chatViewModel.maxTokens)
                }
            }
        }

        // Section 3: Cloud Providers
        item {
            SettingsCardSection(title = "3. Cloud Providers") {
                // Provider profiles list
                chatViewModel.cloudProviders.forEach { profile ->
                    val isSelected = profile.name == chatViewModel.primaryCloudProvider
                    Surface(
                        color = if (isSelected) Color(0xFF64B5F6).copy(alpha = 0.1f) else Color.Transparent,
                        shape = RoundedCornerShape(8.dp),
                        border = if (isSelected) BorderStroke(1.dp, Color(0xFF64B5F6).copy(alpha = 0.5f)) else null,
                        modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)
                    ) {
                        Column(modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                RadioButton(
                                    selected = isSelected,
                                    onClick = {
                                        activity?.savePrimaryCloudProvider(profile.name)
                                        chatViewModel.primaryCloudProvider = profile.name
                                    },
                                    colors = RadioButtonDefaults.colors(selectedColor = Color(0xFF64B5F6))
                                )
                                Column(
                                    modifier = Modifier.weight(1f).clickable {
                                        activity?.savePrimaryCloudProvider(profile.name)
                                        chatViewModel.primaryCloudProvider = profile.name
                                    }
                                ) {
                                    Row(verticalAlignment = Alignment.CenterVertically) {
                                        Text(profile.name, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Bold)
                                        Spacer(Modifier.width(6.dp))
                                        val typeIcon = when (profile.providerType) {
                                            "Gemini" -> "☁️"
                                            "OpenRouter" -> "🌐"
                                            "OpenAI" -> "🧠"
                                            else -> "⚙️"
                                        }
                                        Text("$typeIcon ${profile.providerType}", color = Color.Gray, fontSize = 10.sp)
                                    }
                                    Text(profile.modelId, fontSize = 11.sp, color = Color.Gray, fontFamily = FontFamily.Monospace)
                                }
                                IconButton(
                                    onClick = { chatViewModel.editingProvider = profile; chatViewModel.showAddCloudDialog = true },
                                    modifier = Modifier.size(36.dp)
                                ) {
                                    Icon(Icons.Default.Edit, null, tint = Color.Gray, modifier = Modifier.size(16.dp))
                                }
                                IconButton(
                                    onClick = { activity?.deleteCloudProvider(profile.name) },
                                    modifier = Modifier.size(36.dp)
                                ) {
                                    Icon(Icons.Default.Delete, null, tint = Color(0xFFEF5350), modifier = Modifier.size(16.dp))
                                }
                            }
                            // API Key field for selected provider
                            if (isSelected) {
                                Spacer(Modifier.height(4.dp))
                                var apiKey by remember(profile.name) { mutableStateOf(activity?.getApiKey(profile.name) ?: "") }
                                TextField(
                                    value = apiKey,
                                    onValueChange = { apiKey = it; activity?.saveApiKey(profile.name, it) },
                                    placeholder = { Text("Enter ${profile.name} API Key", fontSize = 11.sp, color = Color.Gray) },
                                    modifier = Modifier.fillMaxWidth().padding(start = 36.dp),
                                    colors = TextFieldDefaults.textFieldColors(
                                        backgroundColor = Color(0xFF1A1C2C),
                                        textColor = Color.White,
                                        focusedIndicatorColor = Color(0xFF64B5F6),
                                        unfocusedIndicatorColor = Color.DarkGray
                                    ),
                                    textStyle = androidx.compose.ui.text.TextStyle(fontSize = 12.sp, fontFamily = FontFamily.Monospace),
                                    singleLine = true,
                                    leadingIcon = { Icon(Icons.Default.Key, null, tint = Color.Gray, modifier = Modifier.size(16.dp)) }
                                )
                            }
                        }
                    }
                }

                if (chatViewModel.cloudProviders.isEmpty()) {
                    Text("No cloud providers configured.", color = Color.DarkGray, fontSize = 12.sp, fontStyle = androidx.compose.ui.text.font.FontStyle.Italic, modifier = Modifier.padding(vertical = 8.dp))
                }

                Spacer(Modifier.height(8.dp))

                // Add Provider button
                OutlinedButton(
                    onClick = { chatViewModel.editingProvider = null; chatViewModel.showAddCloudDialog = true },
                    modifier = Modifier.fillMaxWidth().heightIn(min = 44.dp),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.Cyan),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Icon(Icons.Default.Add, null, tint = Color.Cyan, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text("Add Cloud Provider", fontWeight = FontWeight.Bold)
                }

                Spacer(Modifier.height(8.dp))

                // Connection status & actions
                Row(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text("Active: ${chatViewModel.primaryCloudProvider}", color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Medium)
                        if (chatViewModel.apiLatencyMs > 0) {
                            Text("Latency: ${chatViewModel.apiLatencyMs} ms", color = Color.Cyan, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                        }
                    }
                    val statusColor = when (chatViewModel.apiConnectionStatus) {
                        "Connected" -> Color(0xFF66BB6A)
                        "Error" -> Color(0xFFEF5350)
                        "Testing..." -> Color(0xFFFFCA28)
                        else -> Color.Gray
                    }
                    Surface(
                        color = statusColor.copy(alpha = 0.2f),
                        shape = RoundedCornerShape(6.dp),
                        border = BorderStroke(1.dp, statusColor)
                    ) {
                        Text(
                            chatViewModel.apiConnectionStatus,
                            color = statusColor,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold,
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                        )
                    }
                }

                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = onOpenModelPicker,
                        modifier = Modifier.weight(1f).heightIn(min = 44.dp),
                        colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF25283D)),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Icon(Icons.Default.List, null, tint = Color.Cyan, modifier = Modifier.size(16.dp))
                        Spacer(Modifier.width(4.dp))
                        Text("Models", color = Color.White, fontSize = 12.sp)
                    }
                    Button(
                        onClick = onTestConnection,
                        modifier = Modifier.weight(1f).heightIn(min = 44.dp),
                        colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF25283D)),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Icon(Icons.Default.Sync, null, tint = Color(0xFF66BB6A), modifier = Modifier.size(16.dp))
                        Spacer(Modifier.width(4.dp))
                        Text("Ping", color = Color.White, fontSize = 12.sp)
                    }
                }
            }
        }

        // Section 4: Local Brain
        item {
            SettingsCardSection(title = "4. Local Brain") {
                Text("Installed Models (${chatViewModel.discoveredModels.size})", color = Color.Gray, fontSize = 12.sp)
                Spacer(Modifier.height(4.dp))
                if (chatViewModel.discoveredModels.isEmpty()) {
                    Text("No local models installed.", color = Color.DarkGray, fontSize = 12.sp, fontStyle = androidx.compose.ui.text.font.FontStyle.Italic)
                } else {
                    chatViewModel.discoveredModels.forEach { path ->
                        val isActive = path == chatViewModel.localModelPath
                        Row(modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
                            RadioButton(selected = isActive, onClick = { activity?.hydrateModel(path) }, colors = RadioButtonDefaults.colors(selectedColor = Color(0xFF66BB6A)))
                            Text(File(path).name, modifier = Modifier.weight(1f).clickable { activity?.hydrateModel(path) }, color = if (isActive) Color(0xFF66BB6A) else Color.White, fontSize = 13.sp)
                            IconButton(onClick = { activity?.deleteLocalModel(path) }, modifier = Modifier.size(40.dp)) {
                                Icon(Icons.Default.Delete, null, tint = Color(0xFFEF5350), modifier = Modifier.size(18.dp))
                            }
                        }
                    }
                }
                Spacer(Modifier.height(8.dp))
                OutlinedButton(
                    onClick = { brainPicker.launch(arrayOf("*/*")) },
                    modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = Color(0xFF64B5F6)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Icon(Icons.Default.FileOpen, null, tint = Color(0xFF64B5F6), modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text("Import Brain (.litertlm)", fontWeight = FontWeight.Bold)
                }
            }
        }

        // Section 5: Developer
        item {
            SettingsCardSection(title = "5. Developer") {
                Text("System Prompt", fontSize = 12.sp, color = Color.Gray)
                Spacer(Modifier.height(4.dp))
                TextField(
                    value = chatViewModel.systemPrompt,
                    onValueChange = { chatViewModel.systemPrompt = it; activity?.saveSystemPrompt(it) },
                    modifier = Modifier.fillMaxWidth().height(100.dp),
                    colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF1C1F2E), textColor = Color.White),
                    textStyle = androidx.compose.ui.text.TextStyle(fontSize = 12.sp)
                )
                Spacer(Modifier.height(12.dp))
                Button(
                    onClick = { activity?.nativeEngine?.runNightlyReflection() },
                    modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF6200EE)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Icon(Icons.Default.AutoAwesome, null, tint = Color.White, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text("Run Behavioral Reflection Cycle", color = Color.White, fontSize = 13.sp)
                }
                Spacer(Modifier.height(8.dp))
                OutlinedButton(
                    onClick = { activity?.clearModelCache() },
                    modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = Color(0xFFEF5350)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("Clear All Cache & Logs", color = Color(0xFFEF5350), fontWeight = FontWeight.SemiBold)
                }
            }
        }

        item { Spacer(Modifier.height(24.dp)) }
    }
}

@Composable
fun SettingsCardSection(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column {
        Text(title, color = Color(0xFF64B5F6), fontSize = 14.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(bottom = 6.dp))
        Surface(
            color = Color(0xFF202332),
            shape = RoundedCornerShape(12.dp),
            border = BorderStroke(1.dp, Color(0xFF2D3142)),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                content()
            }
        }
    }
}

@Composable
fun SettingsSwitchRow(label: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, color = Color.White, fontSize = 14.sp)
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = SwitchDefaults.colors(checkedThumbColor = Color(0xFF64B5F6), checkedTrackColor = Color(0xFF64B5F6).copy(alpha = 0.5f))
        )
    }
}

@Composable
fun SettingsSliderRow(label: String, valueText: String, value: Float, valueRange: ClosedFloatingPointRange<Float>, onValueChangeFinished: (Float) -> Unit) {
    var sliderValue by remember(value) { mutableStateOf(value) }
    Column(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = Color.White, fontSize = 13.sp)
            Text(valueText, color = Color(0xFF64B5F6), fontSize = 13.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
        }
        Slider(
            value = sliderValue,
            onValueChange = { sliderValue = it },
            onValueChangeFinished = { onValueChangeFinished(sliderValue) },
            valueRange = valueRange,
            colors = SliderDefaults.colors(thumbColor = Color(0xFF64B5F6), activeTrackColor = Color(0xFF64B5F6))
        )
    }
}

@Composable
fun ModelPicker(
    chatViewModel: ChatViewModel,
    activity: MainActivity?,
    onDismiss: () -> Unit
) {
    var searchQuery by remember { mutableStateOf("") }
    val activeProvider = chatViewModel.cloudProviders.find { it.name == chatViewModel.primaryCloudProvider }
    var selectedModel by remember { mutableStateOf(activeProvider?.modelId ?: "gemini-2.5-flash") }
    val scope = rememberCoroutineScope()

    // Trigger dynamic model fetch on open
    LaunchedEffect(chatViewModel.primaryCloudProvider) {
        if (!chatViewModel.isFetchingModels && chatViewModel.fetchedModels.isEmpty()) {
            chatViewModel.isFetchingModels = true
            try {
                val apiKey = activity?.getApiKey(chatViewModel.primaryCloudProvider) ?: ""
                val models = activity?.nativeEngine?.fetchAvailableModelsAsync(chatViewModel.primaryCloudProvider, apiKey) ?: emptyList()
                chatViewModel.fetchedModels.clear()
                chatViewModel.fetchedModels.addAll(models)
            } catch (_: Exception) { }
            chatViewModel.isFetchingModels = false
        }
    }

    Surface(
        color = Color(0xFF1C1F2E),
        shape = RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp),
        border = BorderStroke(1.dp, Color(0xFF2D3142)),
        modifier = Modifier.fillMaxWidth().fillMaxHeight(0.85f)
    ) {
        Column(modifier = Modifier.padding(16.dp).fillMaxSize()) {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                Column {
                    Text("Select Model", fontSize = 18.sp, fontWeight = FontWeight.Bold, color = Color.White)
                    if (activeProvider != null) {
                        Text("Provider: ${activeProvider.name} (${activeProvider.providerType})", fontSize = 11.sp, color = Color.Gray)
                    }
                }
                IconButton(onClick = onDismiss, modifier = Modifier.size(40.dp)) {
                    Icon(Icons.Default.Close, null, tint = Color.Gray)
                }
            }
            Spacer(Modifier.height(12.dp))
            TextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                placeholder = { Text("Search models...", color = Color.Gray) },
                leadingIcon = { Icon(Icons.Default.Search, null, tint = Color.Gray) },
                modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)),
                colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF25283D), textColor = Color.White, focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent)
            )
            Spacer(Modifier.height(8.dp))

            // Refresh button
            OutlinedButton(
                onClick = {
                    chatViewModel.isFetchingModels = true
                    chatViewModel.fetchedModels.clear()
                    val apiKey = activity?.getApiKey(chatViewModel.primaryCloudProvider) ?: ""
                    // Launch async fetch
                    scope.launch {
                        try {
                            val models = activity?.nativeEngine?.fetchAvailableModelsAsync(chatViewModel.primaryCloudProvider, apiKey) ?: emptyList()
                            chatViewModel.fetchedModels.addAll(models)
                        } catch (_: Exception) { }
                        chatViewModel.isFetchingModels = false
                    }
                },
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.Cyan),
                shape = RoundedCornerShape(8.dp)
            ) {
                if (chatViewModel.isFetchingModels) {
                    CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp, color = Color.Cyan)
                    Spacer(Modifier.width(8.dp))
                    Text("Fetching models...", fontSize = 12.sp)
                } else {
                    Icon(Icons.Default.Refresh, null, tint = Color.Cyan, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(8.dp))
                    Text("Refresh Model List (${chatViewModel.fetchedModels.size} found)", fontSize = 12.sp)
                }
            }
            Spacer(Modifier.height(12.dp))

            LazyColumn(
                modifier = Modifier.weight(1f).fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                // Dynamic fetched models (from active provider)
                val dynamicModels = chatViewModel.fetchedModels.filter { it.contains(searchQuery, ignoreCase = true) }
                if (dynamicModels.isNotEmpty()) {
                    val providerLabel = (activeProvider?.providerType ?: "CLOUD").uppercase()
                    item {
                        Text("$providerLabel MODELS (Dynamic)", color = Color(0xFF64B5F6), fontSize = 11.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(vertical = 4.dp))
                    }
                    items(dynamicModels) { model ->
                        ModelRowItem(model = model, isSelected = selectedModel == model) { selectedModel = model }
                    }
                }

                // Fallback defaults when no dynamic models fetched
                if (chatViewModel.fetchedModels.isEmpty() && !chatViewModel.isFetchingModels) {
                    val providerType = activeProvider?.providerType ?: "Gemini"
                    val defaults = when (providerType) {
                        "Gemini" -> listOf("gemini-2.5-flash", "gemini-2.5-pro", "gemini-2.0-flash", "gemini-1.5-pro", "gemini-1.5-flash")
                        "OpenRouter" -> listOf("google/gemini-2.5-flash", "google/gemini-2.5-pro", "anthropic/claude-sonnet-4", "openai/gpt-4o", "meta-llama/llama-4-maverick")
                        "OpenAI" -> listOf("gpt-4o", "gpt-4o-mini", "gpt-4-turbo", "gpt-3.5-turbo", "o3-mini")
                        else -> listOf("custom-model-1")
                    }.filter { it.contains(searchQuery, ignoreCase = true) }

                    if (defaults.isNotEmpty()) {
                        item {
                            Text("${providerType.uppercase()} DEFAULTS", color = Color(0xFFFFCA28), fontSize = 11.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(vertical = 4.dp))
                            Text("Use 'Refresh' to fetch live model list", color = Color.Gray, fontSize = 10.sp, fontStyle = androidx.compose.ui.text.font.FontStyle.Italic)
                        }
                        items(defaults) { model ->
                            ModelRowItem(model = model, isSelected = selectedModel == model) { selectedModel = model }
                        }
                    }
                }

                // Editable custom model input
                item {
                    Spacer(Modifier.height(8.dp))
                    Text("CUSTOM MODEL ID", color = Color(0xFFCE93D8), fontSize = 11.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(vertical = 4.dp))
                    var customModelId by remember { mutableStateOf("") }
                    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        TextField(
                            value = customModelId,
                            onValueChange = { customModelId = it },
                            placeholder = { Text("e.g. my-custom-model", fontSize = 12.sp, color = Color.Gray) },
                            modifier = Modifier.weight(1f).clip(RoundedCornerShape(8.dp)),
                            colors = TextFieldDefaults.textFieldColors(backgroundColor = Color(0xFF25283D), textColor = Color.White, focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent),
                            textStyle = androidx.compose.ui.text.TextStyle(fontSize = 13.sp, fontFamily = FontFamily.Monospace),
                            singleLine = true
                        )
                        Spacer(Modifier.width(8.dp))
                        Button(
                            onClick = { if (customModelId.isNotBlank()) { selectedModel = customModelId; customModelId = "" } },
                            colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF64B5F6)),
                            shape = RoundedCornerShape(8.dp),
                            modifier = Modifier.heightIn(min = 48.dp)
                        ) {
                            Text("Use", color = Color.Black, fontWeight = FontWeight.Bold)
                        }
                    }
                }

                // Local installed brains
                val localModels = chatViewModel.discoveredModels.map { File(it).name }.filter { it.contains(searchQuery, ignoreCase = true) }
                if (localModels.isNotEmpty()) {
                    item {
                        Text("LOCAL INSTALLED BRAINS", color = Color(0xFF66BB6A), fontSize = 11.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(vertical = 4.dp))
                    }
                    items(localModels) { model ->
                        ModelRowItem(model = model, isSelected = selectedModel == model) { selectedModel = model }
                    }
                }
            }

            Spacer(Modifier.height(16.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedButton(
                    onClick = onDismiss,
                    modifier = Modifier.weight(1f).heightIn(min = 48.dp),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.Gray),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Text("Cancel", fontWeight = FontWeight.Bold)
                }
                Button(
                    onClick = {
                        val activeProfile = chatViewModel.cloudProviders.find { it.name == chatViewModel.primaryCloudProvider }
                        if (activeProfile != null && activity != null) {
                            activity.deleteCloudProvider(activeProfile.name)
                            activity.addCloudProvider(activeProfile.name, activeProfile.providerType, activeProfile.endpoint, selectedModel)
                        }
                        onDismiss()
                    },
                    modifier = Modifier.weight(1f).heightIn(min = 48.dp),
                    colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF64B5F6)),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Text("Save Model", color = Color.Black, fontWeight = FontWeight.Bold)
                }
            }
        }
    }
}

@Composable
fun ModelRowItem(model: String, isSelected: Boolean, onClick: () -> Unit) {
    Surface(
        color = if (isSelected) Color(0xFF64B5F6).copy(alpha = 0.15f) else Color(0xFF25283D),
        shape = RoundedCornerShape(10.dp),
        border = if (isSelected) BorderStroke(1.dp, Color(0xFF64B5F6)) else null,
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(model, color = if (isSelected) Color(0xFF64B5F6) else Color.White, fontSize = 14.sp, fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal)
            RadioButton(selected = isSelected, onClick = onClick, colors = RadioButtonDefaults.colors(selectedColor = Color(0xFF64B5F6)))
        }
    }
}
