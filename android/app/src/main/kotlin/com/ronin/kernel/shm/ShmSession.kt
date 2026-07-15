package com.ronin.kernel.shm

import org.json.JSONArray
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Requirement 1: SHM Session Data Model
 * Complete structured session object preserving exact DSP and Decision Engine outputs.
 */
data class ShmSession(
    val sessionId: String = "ronin_shm_" + System.currentTimeMillis(),
    val schemaVersion: String = "1.0",
    val timestamp: Long = System.currentTimeMillis(),
    val deviceProfile: DeviceProfile = DeviceProfile(),
    val locationProfile: LocationProfile = LocationProfile(),
    val sensorMetadata: SensorMetadata = SensorMetadata(),
    val dspResult: DspResult = DspResult(),
    val features: ShmFeatures = ShmFeatures(),
    val decision: ShmDecision = ShmDecision(),
    val reasoningTrace: ShmReasoningTrace = ShmReasoningTrace()
) {
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("sessionId", sessionId)
            put("schemaVersion", schemaVersion)
            put("timestamp", timestamp)
            put("deviceProfile", JSONObject().apply {
                put("model", deviceProfile.model)
                put("sensorType", deviceProfile.sensorType)
                put("samplingRate", deviceProfile.samplingRate.toDouble())
                put("firmwareInfo", deviceProfile.firmwareInfo)
            })
            put("locationProfile", JSONObject().apply {
                put("buildingId", locationProfile.buildingId)
                put("registeredLocation", locationProfile.registeredLocation)
                put("estimatedLocation", locationProfile.estimatedLocation)
            })
            put("sensorMetadata", JSONObject().apply {
                put("duration", sensorMetadata.duration.toDouble())
                put("sampleCount", sensorMetadata.sampleCount)
                put("axis", sensorMetadata.axis)
            })
            put("dspResult", JSONObject().apply {
                put("detrending", dspResult.detrending)
                put("filtering", dspResult.filtering)
                put("windowFunction", dspResult.windowFunction)
                put("fftResult", JSONArray(dspResult.fftResult))
                put("psdResult", JSONArray(dspResult.psdResult))
                put("top_candidates", JSONArray().apply {
                    dspResult.topCandidates.forEach { c ->
                        put(JSONObject().apply {
                            put("frequency_hz", c.frequencyHz.toDouble())
                            put("psd_db", c.psdValue.toDouble())
                            put("is_anomaly", c.isAnomaly)
                        })
                    }
                })
            })
            put("features", JSONObject().apply {
                put("baselineF0Hz", features.baselineF0Hz.toDouble())
                put("filteredF0Hz", features.filteredF0Hz.toDouble())
                put("noiseFloorDb", features.noiseFloorDb.toDouble())
                put("vibrationEnergy", features.vibrationEnergy.toDouble())
                put("confidence", features.confidence)
            })
            put("decision", JSONObject().apply {
                put("status", decision.status)
                put("healthIndex", decision.healthIndex)
                put("riskLevel", decision.riskLevel)
            })
            put("reasoningTrace", JSONObject().apply {
                val stagesArray = JSONArray()
                reasoningTrace.processingStages.forEach { stagesArray.put(it) }
                put("processingStages", stagesArray)
            })
        }
    }

    companion object {
        fun fromJson(jsonStr: String): ShmSession {
            val root = JSONObject(jsonStr)
            val dev = root.optJSONObject("deviceProfile") ?: JSONObject()
            val loc = root.optJSONObject("locationProfile") ?: JSONObject()
            val sen = root.optJSONObject("sensorMetadata") ?: JSONObject()
            val dsp = root.optJSONObject("dspResult") ?: JSONObject()
            val feat = root.optJSONObject("features") ?: JSONObject()
            val dec = root.optJSONObject("decision") ?: JSONObject()
            val trace = root.optJSONObject("reasoningTrace") ?: JSONObject()

            val fftList = mutableListOf<Float>()
            dsp.optJSONArray("fftResult")?.let { arr ->
                for (i in 0 until arr.length()) fftList.add(arr.optDouble(i).toFloat())
            }
            val psdList = mutableListOf<Float>()
            dsp.optJSONArray("psdResult")?.let { arr ->
                for (i in 0 until arr.length()) psdList.add(arr.optDouble(i).toFloat())
            }
            val stagesList = mutableListOf<String>()
            val sArr = trace.optJSONArray("processingStages") ?: trace.optJSONArray("processing_stages") ?: root.optJSONArray("processingStages") ?: root.optJSONArray("processing_stages")
            sArr?.let { arr ->
                for (i in 0 until arr.length()) stagesList.add(arr.optString(i))
            }
            val candList = mutableListOf<SpectralPeak>()
            dsp.optJSONArray("top_candidates")?.let { arr ->
                for (i in 0 until arr.length()) {
                    val c = arr.optJSONObject(i) ?: continue
                    candList.add(SpectralPeak(
                        frequencyHz = c.optDouble("frequency_hz", 0.0).toFloat(),
                        psdValue = c.optDouble("psd_db", 0.0).toFloat(),
                        isAnomaly = c.optBoolean("is_anomaly", false)
                    ))
                }
            }

            return ShmSession(
                sessionId = root.optString("sessionId", "ronin_shm_${System.currentTimeMillis()}"),
                schemaVersion = root.optString("schemaVersion", "1.0"),
                timestamp = root.optLong("timestamp", System.currentTimeMillis()),
                deviceProfile = DeviceProfile(
                    model = dev.optString("model", android.os.Build.MODEL),
                    sensorType = dev.optString("sensorType", "STMicroelectronics / Bosch 3-Axis Accelerometer"),
                    samplingRate = dev.optDouble("samplingRate", 100.0).toFloat(),
                    firmwareInfo = dev.optString("firmwareInfo", "Ronin Kernel Native DSP")
                ),
                locationProfile = LocationProfile(
                    buildingId = loc.optString("buildingId", "SHM-BUILDING-001"),
                    registeredLocation = loc.optString("registeredLocation", "Single-Story / RC Structure"),
                    estimatedLocation = loc.optString("estimatedLocation", "Ground Floor / Column Center")
                ),
                sensorMetadata = SensorMetadata(
                    duration = sen.optDouble("duration", 10.24).toFloat(),
                    sampleCount = sen.optInt("sampleCount", 1024),
                    axis = sen.optString("axis", "Z (Vertical Normal)")
                ),
                dspResult = DspResult(
                    detrending = dsp.optBoolean("detrending", true),
                    filtering = dsp.optBoolean("filtering", true),
                    windowFunction = dsp.optString("windowFunction", "Hann / Welch PSD (win=1024, sub=512, step=256, nfft=2048)"),
                    fftResult = fftList,
                    psdResult = psdList,
                    topCandidates = candList
                ),
                features = ShmFeatures(
                    baselineF0Hz = feat.optDouble("baselineF0Hz", 1.79).toFloat(),
                    filteredF0Hz = feat.optDouble("filteredF0Hz", 1.79).toFloat(),
                    noiseFloorDb = feat.optDouble("noiseFloorDb", -54.0).toFloat(),
                    vibrationEnergy = feat.optDouble("vibrationEnergy", 0.0142).toFloat(),
                    confidence = feat.optString("confidence", "High")
                ),
                decision = ShmDecision(
                    status = dec.optString("status", "HEALTHY"),
                    healthIndex = dec.optString("healthIndex", "98.5%"),
                    riskLevel = dec.optString("riskLevel", "LOW")
                ),
                reasoningTrace = ShmReasoningTrace(
                    processingStages = stagesList
                )
            )
        }
    }

    /**
     * Requirement 2A: Engineering JSON Export
     */
    fun toEngineeringJson(options: ExportOptions = ExportOptions()): String {
        val root = toJson()
        if (!options.includeGpsLocation) {
            root.remove("locationProfile")
        }
        if (!options.includeDeviceIdentifier) {
            val dev = root.optJSONObject("deviceProfile")
            dev?.put("model", "ANONYMIZED_DEVICE")
        }
        if (!options.includeRawSensorData) {
            val dsp = root.optJSONObject("dspResult")
            dsp?.remove("fftResult")
            dsp?.remove("psdResult")
        }
        if (!options.includeAnalysisMetrics) {
            root.remove("features")
        }
        if (!options.includeDspResults) {
            root.remove("dspResult")
        }
        if (!options.includeDecisionResult) {
            root.remove("decision")
        }
        return root.toString(2)
    }

    /**
     * Requirement Phase 2.2: AI Payload Optimization (Token Limits & Truncation)
     * Extracts critical peaks and downsamples vectors to prevent context window overflow.
     */
    fun toOptimizedAiPayload(options: ExportOptions = ExportOptions(includeRawSensorData = false)): String {
        val root = toJson()
        if (!options.includeGpsLocation) {
            root.remove("locationProfile")
        }
        if (!options.includeDeviceIdentifier) {
            val dev = root.optJSONObject("deviceProfile")
            dev?.put("model", "ANONYMIZED_DEVICE")
        }

        // Optimize DSP result (Replace massive raw arrays with top peaks and downsampled vector)
        val dsp = root.optJSONObject("dspResult")
        if (dsp != null) {
            val psdArr = dsp.optJSONArray("psdResult")
            val peaks = JSONArray()
            if (psdArr != null && psdArr.length() > 0 && sensorMetadata.duration > 0) {
                val freqStep = (deviceProfile.samplingRate / 2.0) / psdArr.length()
                val topIndices = (0 until psdArr.length())
                    .sortedByDescending { psdArr.optDouble(it) }
                    .take(5)
                for (idx in topIndices) {
                    val freq = idx * freqStep
                    peaks.put(JSONObject().apply {
                        put("frequency_hz", "%.2f".format(freq).toDouble())
                        put("psd_value", "%.4f".format(psdArr.optDouble(idx)).toDouble())
                    })
                }
            }
            dsp.remove("fftResult")
            dsp.remove("psdResult")
            dsp.put("extractedTopSpectralPeaks", peaks)

            if (options.includeRawSensorData && dspResult.psdResult.isNotEmpty()) {
                // Heavily downsample to at most 16 representative bins across the frequency spectrum
                val step = kotlin.math.max(1, dspResult.psdResult.size / 16)
                val downsampled = JSONArray()
                for (i in 0 until dspResult.psdResult.size step step) {
                    if (downsampled.length() < 16) {
                        downsampled.put(dspResult.psdResult[i].toDouble())
                    }
                }
                dsp.put("downsampledPsdVector_16bins", downsampled)
            }
        }

        return root.toString(2)
    }

    /**
     * Requirement 2B: Human Report Export
     */
    fun toHumanReport(options: ExportOptions = ExportOptions()): String {
        val dateStr = SimpleDateFormat("yyyy-MM-dd HH:mm:ss z", Locale.US).format(Date(timestamp))
        val sb = StringBuilder()
        sb.append("==========================================\n")
        sb.append("Ronin SHM Report (Schema v$schemaVersion)\n")
        sb.append("==========================================\n\n")
        sb.append("Session ID: $sessionId\n")
        sb.append("Timestamp: $dateStr\n")
        if (options.includeGpsLocation) {
            sb.append("Building ID: ${locationProfile.buildingId}\n")
            sb.append("Location: ${locationProfile.registeredLocation} (${locationProfile.estimatedLocation})\n")
        }
        if (options.includeDeviceIdentifier) {
            sb.append("Device: ${deviceProfile.model} | Sensor: ${deviceProfile.sensorType}\n")
        }
        sb.append("\n")

        if (options.includeDecisionResult) {
            sb.append("Status:\n")
            sb.append("${decision.status}\n\n")
            sb.append("Health Index:\n")
            sb.append("${decision.healthIndex}\n\n")
            sb.append("Risk Level:\n")
            sb.append("${decision.riskLevel}\n\n")
        }

        if (options.includeAnalysisMetrics) {
            sb.append("Dominant Frequency:\n")
            sb.append("${"%.2f".format(features.filteredF0Hz)} Hz\n\n")
            sb.append("Noise Floor:\n")
            sb.append("${"%.1f".format(features.noiseFloorDb)} dB\n\n")
            sb.append("Vibration Energy:\n")
            sb.append("${"%.4f".format(features.vibrationEnergy)}\n\n")
            sb.append("Confidence:\n")
            sb.append("${features.confidence}\n\n")
        }

        if (options.includeDspResults) {
            sb.append("--- DSP Configuration Summary ---\n")
            sb.append("Sampling Rate: ${deviceProfile.samplingRate} Hz\n")
            sb.append("Duration: ${sensorMetadata.duration} s (${sensorMetadata.sampleCount} samples)\n")
            sb.append("Windowing & Welch: ${dspResult.windowFunction}\n")
            sb.append("Detrending Active: ${dspResult.detrending} | Filtering Active: ${dspResult.filtering}\n\n")
        }

        sb.append("==========================================\n")
        sb.append("Generated by Ronin Kernel v10.2.17 AI OS\n")
        sb.append("==========================================\n")
        return sb.toString()
    }

    /**
     * Requirement 2C: Developer Debug Export
     */
    fun toDeveloperDebugLog(): String {
        val dateStr = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS z", Locale.US).format(Date(timestamp))
        val sb = StringBuilder()
        sb.append("==========================================\n")
        sb.append("RONIN SHM DEVELOPER DEBUG EXPORT\n")
        sb.append("==========================================\n")
        sb.append("Session ID: $sessionId\n")
        sb.append("Execution Timestamp: $dateStr\n")
        sb.append("Firmware Info: ${deviceProfile.firmwareInfo}\n")
        sb.append("Device Profile: ${deviceProfile.model} (${deviceProfile.sensorType} @ ${deviceProfile.samplingRate}Hz)\n")
        sb.append("Sensor Metadata: Axis=${sensorMetadata.axis}, Samples=${sensorMetadata.sampleCount}, Duration=${sensorMetadata.duration}s\n\n")

        sb.append("[DSP PIPELINE CONFIGURATION]\n")
        sb.append(" - Detrending: ${dspResult.detrending}\n")
        sb.append(" - Filtering: ${dspResult.filtering}\n")
        sb.append(" - Window/Welch Configuration: ${dspResult.windowFunction}\n")
        sb.append(" - FFT Result Vector Size: ${dspResult.fftResult.size}\n")
        sb.append(" - PSD Vector Size: ${dspResult.psdResult.size}\n\n")

        sb.append("[EXTRACTED MODAL FEATURES]\n")
        sb.append(" - Baseline f0: ${features.baselineF0Hz} Hz\n")
        sb.append(" - Filtered f0: ${features.filteredF0Hz} Hz\n")
        sb.append(" - Noise Floor: ${features.noiseFloorDb} dB\n")
        sb.append(" - Total Vibration Energy: ${features.vibrationEnergy}\n")
        sb.append(" - Modal Confidence Score: ${features.confidence}\n\n")

        sb.append("[DECISION ENGINE OUTPUT]\n")
        sb.append(" - Structural Health Status: ${decision.status}\n")
        sb.append(" - Health Index: ${decision.healthIndex}\n")
        sb.append(" - Assessed Risk Level: ${decision.riskLevel}\n\n")

        sb.append("[REASONING TRACE & PIPELINE STAGES]\n")
        reasoningTrace.processingStages.forEachIndexed { idx, stage ->
            sb.append(" Stage ${idx + 1}: $stage\n")
        }
        sb.append("\n==========================================\n")
        sb.append("END OF DEBUG LOG\n")
        sb.append("==========================================\n")
        return sb.toString()
    }
}

data class DeviceProfile(
    val model: String = android.os.Build.MODEL,
    val sensorType: String = "STMicroelectronics / Bosch 3-Axis Accelerometer",
    val samplingRate: Float = 100.0f,
    val firmwareInfo: String = "Ronin Kernel v10.2.17 Native DSP"
)

data class LocationProfile(
    val buildingId: String = "SHM-BUILDING-001",
    val registeredLocation: String = "Single-Story / RC Structure",
    val estimatedLocation: String = "Ground Floor / Column Center"
)

data class SensorMetadata(
    val duration: Float = 10.24f,
    val sampleCount: Int = 1024,
    val axis: String = "Z (Vertical Normal)"
)

data class DspResult(
    val detrending: Boolean = true,
    val filtering: Boolean = true,
    val windowFunction: String = "Hann / Welch PSD (win=1024, sub=512, step=256, nfft=2048)",
    val fftResult: List<Float> = emptyList(),
    val psdResult: List<Float> = emptyList(),
    val topCandidates: List<SpectralPeak> = emptyList()
)

data class ShmFeatures(
    val baselineF0Hz: Float = 1.79f,
    val filteredF0Hz: Float = 1.79f,
    val noiseFloorDb: Float = -54.0f,
    val vibrationEnergy: Float = 0.0142f,
    val confidence: String = "High (CV < 0.2%)"
)

data class ShmDecision(
    val status: String = "HEALTHY",
    val healthIndex: String = "98.5%",
    val riskLevel: String = "LOW"
)

data class ShmReasoningTrace(
    val processingStages: List<String> = emptyList()
)

/**
 * Requirement 8: Privacy Controls options
 */
data class ExportOptions(
    val includeAnalysisMetrics: Boolean = true,
    val includeDspResults: Boolean = true,
    val includeDecisionResult: Boolean = true,
    val includeGpsLocation: Boolean = false,
    val includeDeviceIdentifier: Boolean = false,
    val includeRawSensorData: Boolean = false
)
