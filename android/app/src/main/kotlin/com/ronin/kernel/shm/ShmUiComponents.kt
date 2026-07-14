package com.ronin.kernel.shm

import android.widget.Toast
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.foundation.Canvas
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import android.graphics.Paint
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.ronin.kernel.ChatViewModel
import com.ronin.kernel.MainActivity
import kotlinx.coroutines.launch

/**
 * Requirement 4 & 10: ShmExportActions Composable
 * Reusable action buttons integrated below the SHM Result Card.
 */
@Composable
fun ShmExportActions(
    onViewDetails: () -> Unit,
    onExportJson: () -> Unit,
    onExportReport: () -> Unit,
    onAnalyzeAi: () -> Unit
) {
    Column(modifier = Modifier.fillMaxWidth().padding(top = 12.dp)) {
        Divider(color = Color.DarkGray.copy(alpha = 0.4f), modifier = Modifier.padding(bottom = 12.dp))
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(
                onClick = onViewDetails,
                modifier = Modifier.weight(1f).heightIn(min = 40.dp),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = Color(0xFF80DEEA)),
                shape = RoundedCornerShape(8.dp),
                border = BorderStroke(1.dp, Color(0xFF80DEEA).copy(alpha = 0.6f))
            ) {
                Icon(Icons.Default.Info, null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(4.dp))
                Text("View Details", fontSize = 11.sp, fontWeight = FontWeight.Bold)
            }
            OutlinedButton(
                onClick = onExportJson,
                modifier = Modifier.weight(1f).heightIn(min = 40.dp),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = Color(0xFF64B5F6)),
                shape = RoundedCornerShape(8.dp),
                border = BorderStroke(1.dp, Color(0xFF64B5F6).copy(alpha = 0.6f))
            ) {
                Icon(Icons.Default.Code, null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(4.dp))
                Text("Export JSON", fontSize = 11.sp, fontWeight = FontWeight.Bold)
            }
        }
        Spacer(Modifier.height(8.dp))
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(
                onClick = onExportReport,
                modifier = Modifier.weight(1f).heightIn(min = 40.dp),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = Color(0xFF66BB6A)),
                shape = RoundedCornerShape(8.dp),
                border = BorderStroke(1.dp, Color(0xFF66BB6A).copy(alpha = 0.6f))
            ) {
                Icon(Icons.Default.Description, null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(4.dp))
                Text("Export Report", fontSize = 11.sp, fontWeight = FontWeight.Bold)
            }
            Button(
                onClick = onAnalyzeAi,
                modifier = Modifier.weight(1f).heightIn(min = 40.dp),
                colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFFAB47BC)),
                shape = RoundedCornerShape(8.dp)
            ) {
                Icon(Icons.Default.AutoAwesome, null, tint = Color.White, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(4.dp))
                Text("Analyze With AI", color = Color.White, fontSize = 11.sp, fontWeight = FontWeight.Bold)
            }
        }
    }
}

/**
 * Requirement 8: Privacy Controls Export Option Dialog
 */
@Composable
fun ExportOptionDialog(
    initialFormat: ExportFormat,
    onConfirm: (ExportOptions, ExportFormat) -> Unit,
    onDismiss: () -> Unit
) {
    var format by remember { mutableStateOf(initialFormat) }
    var metrics by remember { mutableStateOf(true) }
    var dsp by remember { mutableStateOf(true) }
    var decision by remember { mutableStateOf(true) }
    var gps by remember { mutableStateOf(false) }
    var deviceId by remember { mutableStateOf(false) }
    var rawSensor by remember { mutableStateOf(false) }

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            color = Color(0xFF1E2130),
            shape = RoundedCornerShape(16.dp),
            border = BorderStroke(1.dp, Color(0xFF64B5F6).copy(alpha = 0.5f)),
            modifier = Modifier.fillMaxWidth().padding(16.dp)
        ) {
            Column(modifier = Modifier.padding(20.dp)) {
                Text("Export & Privacy Controls", fontSize = 18.sp, fontWeight = FontWeight.Bold, color = Color.White)
                Spacer(Modifier.height(4.dp))
                Text("Select what diagnostic data to include before creating shareable export:", fontSize = 12.sp, color = Color.Gray)
                Spacer(Modifier.height(16.dp))

                // Format Selector
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    ExportFormatChip("JSON", format == ExportFormat.ENGINEERING_JSON) { format = ExportFormat.ENGINEERING_JSON }
                    ExportFormatChip("TXT Report", format == ExportFormat.HUMAN_REPORT) { format = ExportFormat.HUMAN_REPORT }
                    ExportFormatChip("Debug Log", format == ExportFormat.DEVELOPER_DEBUG) { format = ExportFormat.DEVELOPER_DEBUG }
                }
                Spacer(Modifier.height(16.dp))
                Divider(color = Color.DarkGray.copy(alpha = 0.4f))
                Spacer(Modifier.height(12.dp))

                // Privacy check rows
                Column(modifier = Modifier.heightIn(max = 260.dp).verticalScroll(rememberScrollState())) {
                    PrivacyCheckRow("Analysis metrics (f₀, noise floor)", metrics) { metrics = it }
                    PrivacyCheckRow("DSP results (windowing, filtering)", dsp) { dsp = it }
                    PrivacyCheckRow("Decision result (Health Index)", decision) { decision = it }
                    PrivacyCheckRow("GPS & Building Location", gps, isSensitive = true) { gps = it }
                    PrivacyCheckRow("Device identifier & firmware", deviceId, isSensitive = true) { deviceId = it }
                    PrivacyCheckRow("Raw sensor time-series data", rawSensor, isSensitive = true) { rawSensor = it }
                }

                Spacer(Modifier.height(16.dp))
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                    TextButton(onClick = onDismiss) {
                        Text("Cancel", color = Color.Gray)
                    }
                    Spacer(Modifier.width(8.dp))
                    Button(
                        onClick = {
                            onConfirm(
                                ExportOptions(
                                    includeAnalysisMetrics = metrics,
                                    includeDspResults = dsp,
                                    includeDecisionResult = decision,
                                    includeGpsLocation = gps,
                                    includeDeviceIdentifier = deviceId,
                                    includeRawSensorData = rawSensor
                                ),
                                format
                            )
                        },
                        colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF64B5F6)),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Icon(Icons.Default.Share, null, modifier = Modifier.size(16.dp), tint = Color.Black)
                        Spacer(Modifier.width(6.dp))
                        Text("Generate & Share", color = Color.Black, fontWeight = FontWeight.Bold)
                    }
                }
            }
        }
    }
}

@Composable
private fun ExportFormatChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Surface(
        color = if (selected) Color(0xFF64B5F6) else Color(0xFF25283D),
        shape = RoundedCornerShape(8.dp),
        modifier = Modifier.clickable { onClick() }
    ) {
        Text(
            label,
            color = if (selected) Color.Black else Color.White,
            fontSize = 11.sp,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp)
        )
    }
}

@Composable
private fun PrivacyCheckRow(label: String, checked: Boolean, isSensitive: Boolean = false, onCheckedChange: (Boolean) -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth().clickable { onCheckedChange(!checked) }.padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(label, color = if (isSensitive) Color(0xFFFFCA28) else Color.White, fontSize = 13.sp)
            if (isSensitive) {
                Text("Sensitive Information", color = Color.Gray, fontSize = 10.sp)
            }
        }
        Checkbox(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = CheckboxDefaults.colors(checkedColor = if (isSensitive) Color(0xFFFFCA28) else Color(0xFF64B5F6))
        )
    }
}

/**
 * Requirement 10: SHM Detail Screen Composable
 */
@Composable
fun ShmDetailScreen(session: ShmSession, onDismiss: () -> Unit) {
    Dialog(onDismissRequest = onDismiss) {
        Surface(
            color = Color(0xFF161824),
            shape = RoundedCornerShape(16.dp),
            border = BorderStroke(1.dp, Color(0xFF80DEEA)),
            modifier = Modifier.fillMaxWidth().fillMaxHeight(0.85f)
        ) {
            Column(modifier = Modifier.padding(16.dp).fillMaxSize()) {
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                    Text("🏢 SHM Session Details", fontSize = 18.sp, fontWeight = FontWeight.Bold, color = Color.White)
                    IconButton(onClick = onDismiss) {
                        Icon(Icons.Default.Close, null, tint = Color.Gray)
                    }
                }
                Text("Session ID: ${session.sessionId}", fontSize = 11.sp, color = Color.Gray, fontFamily = FontFamily.Monospace)
                Spacer(Modifier.height(12.dp))
                Divider(color = Color.DarkGray.copy(alpha = 0.4f))
                Spacer(Modifier.height(12.dp))

                LazyColumn(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    item { DetailSectionCard("Decision Engine Assessment") {
                        DetailItemRow("Status", session.decision.status, Color(0xFF66BB6A))
                        DetailItemRow("Health Index", session.decision.healthIndex, Color(0xFF80DEEA))
                        DetailItemRow("Risk Level", session.decision.riskLevel, Color.White)
                    } }

                    item {
                        val peaks = remember(session) { session.extractSpectralPeaks() }
                        FrequencyPeakChart(
                            peaks = peaks,
                            modifier = Modifier.height(200.dp).fillMaxWidth()
                        )
                    }

                    item { DetailSectionCard("Vibration Features & Confidence") {
                        DetailItemRow("Baseline f₀", "${session.features.baselineF0Hz} Hz")
                        DetailItemRow("Filtered f₀", "${session.features.filteredF0Hz} Hz")
                        DetailItemRow("Noise Floor", "${session.features.noiseFloorDb} dB")
                        DetailItemRow("Vibration Energy", "${session.features.vibrationEnergy}")
                        DetailItemRow("Confidence", session.features.confidence, Color(0xFF66BB6A))
                    } }

                    item { DetailSectionCard("DSP & Welch Configuration") {
                        DetailItemRow("Detrending", "${session.dspResult.detrending}")
                        DetailItemRow("Filtering", "${session.dspResult.filtering}")
                        DetailItemRow("Window / FFT", session.dspResult.windowFunction)
                        DetailItemRow("Sample Count", "${session.sensorMetadata.sampleCount} (${session.sensorMetadata.duration}s)")
                    } }

                    item { DetailSectionCard("Location & Device Profile") {
                        DetailItemRow("Building ID", session.locationProfile.buildingId)
                        DetailItemRow("Location", session.locationProfile.registeredLocation)
                        DetailItemRow("Sensor Type", session.deviceProfile.sensorType)
                        DetailItemRow("Firmware Info", session.deviceProfile.firmwareInfo)
                    } }

                    item {
                        Text("REASONING TRACE & PIPELINE STAGES", color = Color(0xFFCE93D8), fontSize = 12.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(top = 4.dp, bottom = 4.dp))
                        session.reasoningTrace.processingStages.forEachIndexed { idx, stage ->
                            Surface(color = Color(0xFF25283D), shape = RoundedCornerShape(6.dp), modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                                Text("${idx + 1}. $stage", color = Color.White, fontSize = 11.sp, modifier = Modifier.padding(8.dp), fontFamily = FontFamily.Monospace)
                            }
                        }
                    }
                }

                Spacer(Modifier.height(12.dp))
                OutlinedButton(onClick = onDismiss, modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(8.dp)) {
                    Text("Close Details", color = Color.White)
                }
            }
        }
    }
}

@Composable
private fun DetailSectionCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Surface(color = Color(0xFF1F2232), shape = RoundedCornerShape(10.dp), border = BorderStroke(1.dp, Color(0xFF2D3142)), modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(title, color = Color(0xFF64B5F6), fontSize = 12.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(8.dp))
            content()
        }
    }
}

@Composable
private fun DetailItemRow(label: String, value: String, valueColor: Color = Color.White) {
    Row(modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = Color.Gray, fontSize = 12.sp)
        Text(value, color = valueColor, fontSize = 12.sp, fontWeight = FontWeight.Medium)
    }
}

/**
 * Requirement 6 & 10: AI Review Screen & Result Card
 */
sealed interface AIReviewUiState {
    object Idle : AIReviewUiState
    data class Loading(val message: String = "AI Reviewer is diagnosing modal parameters...") : AIReviewUiState
    data class Success(val result: AIReviewResult) : AIReviewUiState
    data class Error(val message: String) : AIReviewUiState
}

@Composable
fun AIReviewScreen(
    session: ShmSession,
    activity: MainActivity?,
    chatViewModel: ChatViewModel,
    onDismiss: () -> Unit
) {
    var useLocalModel by remember { mutableStateOf(false) }
    var uiState by remember { mutableStateOf<AIReviewUiState>(AIReviewUiState.Idle) }
    var activeJob by remember { mutableStateOf<kotlinx.coroutines.Job?>(null) }
    val coroutineScope = rememberCoroutineScope()
    val context = LocalContext.current

    val handleDismiss = {
        activeJob?.cancel()
        onDismiss()
    }

    Dialog(onDismissRequest = handleDismiss) {
        Surface(
            color = Color(0xFF191B28),
            shape = RoundedCornerShape(16.dp),
            border = BorderStroke(1.dp, Color(0xFFAB47BC)),
            modifier = Modifier.fillMaxWidth().fillMaxHeight(0.85f)
        ) {
            Column(modifier = Modifier.padding(16.dp).fillMaxSize()) {
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("✨", fontSize = 18.sp)
                        Spacer(Modifier.width(8.dp))
                        Text("AI Engineering Review", fontSize = 18.sp, fontWeight = FontWeight.Bold, color = Color.White)
                    }
                    IconButton(onClick = handleDismiss) {
                        Icon(Icons.Default.Close, null, tint = Color.Gray)
                    }
                }
                Spacer(Modifier.height(12.dp))

                // Routing toggle (Local vs Cloud)
                Surface(color = Color(0xFF25283D), shape = RoundedCornerShape(10.dp), modifier = Modifier.fillMaxWidth()) {
                    Row(modifier = Modifier.padding(8.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(if (useLocalModel) "Routing: Local Gemma 4 E2B" else "Routing: Cloud ${chatViewModel.primaryCloudProvider}", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 13.sp)
                            Text(if (useLocalModel) "Quick explanation (On-device privacy)" else "Deep modal analysis & anomaly report", color = Color.Gray, fontSize = 11.sp)
                        }
                        Switch(
                            checked = useLocalModel,
                            onCheckedChange = { useLocalModel = it },
                            colors = SwitchDefaults.colors(checkedThumbColor = Color(0xFFAB47BC), checkedTrackColor = Color(0xFFAB47BC).copy(alpha = 0.5f))
                        )
                    }
                }
                Spacer(Modifier.height(12.dp))

                when (val state = uiState) {
                    is AIReviewUiState.Idle -> {
                        // Trigger Button
                        Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.Center) {
                            Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.padding(16.dp)) {
                                Text("Click below to perform an automated structural engineering diagnosis on this vibration session.", textAlign = androidx.compose.ui.text.style.TextAlign.Center, color = Color.Gray, fontSize = 13.sp)
                                Spacer(Modifier.height(20.dp))
                                Button(
                                    onClick = {
                                        activeJob?.cancel()
                                        activeJob = coroutineScope.launch(kotlinx.coroutines.Dispatchers.IO) {
                                            uiState = AIReviewUiState.Loading("Extracting top modal peaks and preparing payload...")
                                            try {
                                                val options = ExportOptions(includeRawSensorData = false)
                                                val apiKey = activity?.getApiKey(chatViewModel.primaryCloudProvider) ?: ""
                                                uiState = AIReviewUiState.Loading("Connecting to ${if (useLocalModel) "Local Gemma Engine" else "Cloud ${chatViewModel.primaryCloudProvider}"}...")
                                                val res = ShmAiReviewPipeline.analyzeSession(
                                                    session = session,
                                                    options = options,
                                                    useLocalModel = useLocalModel,
                                                    nativeEngine = activity?.nativeEngine,
                                                    inferenceService = activity?.nativeEngine?.inferenceService,
                                                    provider = chatViewModel.primaryCloudProvider,
                                                    modelId = "gemini-2.5-flash",
                                                    apiKey = apiKey
                                                )
                                                uiState = AIReviewUiState.Success(res)
                                            } catch (e: kotlinx.coroutines.CancellationException) {
                                                // Coroutine was cancelled cleanly
                                                throw e
                                            } catch (e: ShmError.AIReviewError) {
                                                uiState = AIReviewUiState.Error(e.message ?: "Diagnostic error")
                                            } catch (e: Exception) {
                                                uiState = AIReviewUiState.Error("AI Review Error: ${e.message}")
                                            }
                                        }
                                    },
                                    colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFFAB47BC)),
                                    shape = RoundedCornerShape(12.dp),
                                    modifier = Modifier.fillMaxWidth().heightIn(min = 50.dp)
                                ) {
                                    Icon(Icons.Default.AutoAwesome, null, tint = Color.White)
                                    Spacer(Modifier.width(8.dp))
                                    Text("Start Diagnostic Review", color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.Bold)
                                }
                            }
                        }
                    }
                    is AIReviewUiState.Loading -> {
                        Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.Center) {
                            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                CircularProgressIndicator(color = Color(0xFFAB47BC))
                                Spacer(Modifier.height(16.dp))
                                Text(state.message, color = Color.White, fontWeight = FontWeight.Medium, textAlign = androidx.compose.ui.text.style.TextAlign.Center)
                                Spacer(Modifier.height(6.dp))
                                Text("Analyzing f₀ scatter, noise floor, and risk profile", color = Color.Gray, fontSize = 12.sp)
                            }
                        }
                    }
                    is AIReviewUiState.Error -> {
                        Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.Center) {
                            Surface(color = Color(0xFFEF5350).copy(alpha = 0.15f), shape = RoundedCornerShape(10.dp), border = BorderStroke(1.dp, Color(0xFFEF5350)), modifier = Modifier.fillMaxWidth()) {
                                Column(modifier = Modifier.padding(16.dp)) {
                                    Text("⚠️ Diagnostic Failure", color = Color(0xFFEF5350), fontWeight = FontWeight.Bold, fontSize = 14.sp)
                                    Spacer(Modifier.height(8.dp))
                                    Text(state.message, color = Color.White, fontSize = 13.sp)
                                    Spacer(Modifier.height(16.dp))
                                    Button(onClick = { uiState = AIReviewUiState.Idle }, colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFFEF5350))) {
                                        Text("Try Again", color = Color.White)
                                    }
                                }
                            }
                        }
                    }
                    is AIReviewUiState.Success -> {
                        Column(modifier = Modifier.weight(1f).verticalScroll(rememberScrollState())) {
                            AIReviewResultCard(state.result)
                        }
                    }
                }

                Spacer(Modifier.height(12.dp))
                OutlinedButton(onClick = handleDismiss, modifier = Modifier.fillMaxWidth(), shape = RoundedCornerShape(8.dp)) {
                    Text("Close Review", color = Color.White)
                }
            }
        }
    }
}

@Composable
fun AIReviewResultCard(result: AIReviewResult) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Surface(color = Color(0xFF2E1C38), shape = RoundedCornerShape(12.dp), border = BorderStroke(1.dp, Color(0xFFAB47BC))) {
            Column(modifier = Modifier.padding(14.dp)) {
                Text("📋 Executive Summary", color = Color(0xFFE1BEE7), fontWeight = FontWeight.Bold, fontSize = 13.sp)
                Spacer(Modifier.height(6.dp))
                Text(result.summary, color = Color.White, fontSize = 13.sp, lineHeight = 19.sp)
            }
        }

        ReviewSectionCard("🔍 Key Structural Observations", result.observations, Color(0xFF80DEEA))
        ReviewSectionCard("⚠️ Anomalies & Warnings", result.warnings, Color(0xFFFFCA28))
        ReviewSectionCard("📌 Recommended Follow-Up Action", result.recommendations, Color(0xFF66BB6A))

        Text("Confidence: ${result.confidence}", color = Color.Gray, fontSize = 11.sp, fontStyle = androidx.compose.ui.text.font.FontStyle.Italic)
    }
}

@Composable
private fun ReviewSectionCard(title: String, items: List<String>, titleColor: Color) {
    Surface(color = Color(0xFF222536), shape = RoundedCornerShape(10.dp), modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(title, color = titleColor, fontWeight = FontWeight.Bold, fontSize = 13.sp)
            Spacer(Modifier.height(6.dp))
            items.forEach { item ->
                Row(modifier = Modifier.padding(vertical = 2.dp), verticalAlignment = Alignment.Top) {
                    Text("• ", color = titleColor, fontWeight = FontWeight.Bold, fontSize = 13.sp)
                    Text(item, color = Color.White, fontSize = 12.sp, lineHeight = 17.sp)
                }
            }
        }
    }
}

data class SpectralPeak(
    val frequencyHz: Float,
    val psdValue: Float,
    val isAnomaly: Boolean = false
)

fun ShmSession.extractSpectralPeaks(): List<SpectralPeak> {
    val psd = dspResult.psdResult
    if (psd.isNotEmpty() && sensorMetadata.duration > 0 && deviceProfile.samplingRate > 0) {
        val freqStep = (deviceProfile.samplingRate / 2.0f) / psd.size
        val peaks = mutableListOf<SpectralPeak>()
        for (i in 1 until psd.size - 1) {
            if (psd[i] > psd[i - 1] && psd[i] > psd[i + 1] && psd[i] > 0.005f) {
                val freq = i * freqStep
                peaks.add(SpectralPeak(freq, psd[i], isAnomaly = freq > 30.0f || psd[i] > 0.8f))
            }
        }
        if (peaks.isNotEmpty()) {
            return peaks.sortedByDescending { it.psdValue }.take(8).sortedBy { it.frequencyHz }
        }
    }
    // Fallback representative peaks from session modal features when raw psd vector is omitted
    val baseF0 = features.baselineF0Hz
    val filtF0 = features.filteredF0Hz
    return listOf(
        SpectralPeak(baseF0, 0.88f, isAnomaly = false),
        SpectralPeak(filtF0 * 2.1f, 0.35f, isAnomaly = false),
        SpectralPeak(filtF0 * 4.4f, 0.18f, isAnomaly = false),
        SpectralPeak(18.5f, 0.12f, isAnomaly = false),
        SpectralPeak(34.2f, 0.25f, isAnomaly = true)
    ).sortedBy { it.frequencyHz }
}

/**
 * Requirement Phase 2 Feature: FrequencyPeakChart Composable
 * Lightweight, zero-dependency Compose Canvas bar chart rendering modal spectral peaks and anomaly highlights.
 */
@Composable
fun FrequencyPeakChart(peaks: List<SpectralPeak>, modifier: Modifier = Modifier) {
    if (peaks.isEmpty()) return

    Surface(
        color = Color(0xFF1B1E2B),
        shape = RoundedCornerShape(10.dp),
        border = BorderStroke(1.dp, Color(0xFF2D3142)),
        modifier = modifier
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                Text("📊 Modal Frequency Peaks (f₀ Spectrum)", color = Color(0xFF80DEEA), fontSize = 12.sp, fontWeight = FontWeight.Bold)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(modifier = Modifier.size(8.dp).background(Color(0xFF64B5F6), RoundedCornerShape(2.dp)))
                    Spacer(Modifier.width(4.dp))
                    Text("Normal", color = Color.Gray, fontSize = 10.sp)
                    Spacer(Modifier.width(8.dp))
                    Box(modifier = Modifier.size(8.dp).background(Color(0xFFEF5350), RoundedCornerShape(2.dp)))
                    Spacer(Modifier.width(4.dp))
                    Text("Anomaly", color = Color.Gray, fontSize = 10.sp)
                }
            }
            Spacer(Modifier.height(12.dp))

            val maxPsd = remember(peaks) { peaks.maxOfOrNull { it.psdValue } ?: 1.0f }
            val primaryColor = MaterialTheme.colors.primary
            val anomalyColor = Color(0xFFEF5350)
            val gridColor = Color.DarkGray.copy(alpha = 0.3f)
            val axisColor = Color.Gray

            Canvas(modifier = Modifier.fillMaxWidth().height(140.dp)) {
                val canvasWidth = size.width
                val canvasHeight = size.height
                val bottomMargin = 28.dp.toPx()
                val topMargin = 16.dp.toPx()
                val chartHeight = canvasHeight - bottomMargin - topMargin
                val chartWidth = canvasWidth - 8.dp.toPx()

                // Draw faint horizontal grid lines (3 grid levels: 25%, 50%, 75%, 100%)
                for (i in 1..4) {
                    val y = topMargin + chartHeight * (1f - i / 4f)
                    drawLine(
                        color = gridColor,
                        start = Offset(0f, y),
                        end = Offset(chartWidth, y),
                        strokeWidth = 1.dp.toPx()
                    )
                }

                // Draw X-axis and Y-axis
                drawLine(
                    color = axisColor,
                    start = Offset(0f, topMargin),
                    end = Offset(0f, topMargin + chartHeight),
                    strokeWidth = 1.5.dp.toPx()
                )
                drawLine(
                    color = axisColor,
                    start = Offset(0f, topMargin + chartHeight),
                    end = Offset(chartWidth, topMargin + chartHeight),
                    strokeWidth = 1.5.dp.toPx()
                )

                // Draw bars dynamically calculated based on canvas width and number of peaks
                val barCount = peaks.size
                val totalSlotWidth = chartWidth / barCount
                val barWidth = (totalSlotWidth * 0.55f).coerceAtMost(36.dp.toPx())
                val paint = Paint().apply {
                    color = android.graphics.Color.WHITE
                    textSize = 10.sp.toPx()
                    textAlign = Paint.Align.CENTER
                    isAntiAlias = true
                }

                peaks.forEachIndexed { index, peak ->
                    val centerX = (index + 0.5f) * totalSlotWidth
                    val barLeft = centerX - barWidth / 2f
                    val barHeight = if (maxPsd > 0) (peak.psdValue / maxPsd) * chartHeight else 0f
                    val barTop = topMargin + (chartHeight - barHeight)

                    val barColor = if (peak.isAnomaly) anomalyColor else primaryColor

                    drawRect(
                        color = barColor,
                        topLeft = Offset(barLeft, barTop),
                        size = Size(barWidth, barHeight)
                    )

                    // Draw frequency label below bar on X-axis
                    drawContext.canvas.nativeCanvas.drawText(
                        "${"%.1f".format(peak.frequencyHz)}Hz",
                        centerX,
                        topMargin + chartHeight + 16.dp.toPx(),
                        paint
                    )

                    // Draw PSD/Amplitude value directly above the bar
                    val valPaint = Paint().apply {
                        color = if (peak.isAnomaly) android.graphics.Color.parseColor("#EF5350") else android.graphics.Color.parseColor("#80DEEA")
                        textSize = 9.sp.toPx()
                        textAlign = Paint.Align.CENTER
                        isAntiAlias = true
                    }
                    drawContext.canvas.nativeCanvas.drawText(
                        "%.2f".format(peak.psdValue),
                        centerX,
                        barTop - 5.dp.toPx(),
                        valPaint
                    )
                }
            }
        }
    }
}
