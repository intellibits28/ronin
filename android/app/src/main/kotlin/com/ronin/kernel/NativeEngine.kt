package com.ronin.kernel

import android.content.Context
import android.util.Log
import android.content.ComponentCallbacks2
import android.content.res.Configuration
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import android.os.RemoteException
import androidx.annotation.Keep
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import org.json.JSONArray
import org.json.JSONObject
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

sealed class BridgeResult<out T> {
    data class Success<out T>(val value: T) : BridgeResult<T>()
    data class Error(val code: String, val message: String) : BridgeResult<Nothing>()
}

/**
 * Native Engine (Hardened v4.0 Architecture)
 * Optimized for Mid-range stability with OkHttp networking and process isolation.
 */
class NativeEngine(private val context: Context) : ComponentCallbacks2 {

    private val dbHelper = DatabaseHelper(context)
    private val drivers = mutableMapOf<String, ICapabilityDriver>()
    private val securityProvider = SecurityProvider()

    init {
        // v7.0 Layer 4: Register Android Drivers
        drivers["LOCATION"] = LocationDriver(context)
        drivers["MAP"] = LocationDriver(context) // Re-use location driver for map display trigger
        drivers["SMS"] = SmsDriver(context)
        
        // v8.2: Pipeline Diagnostic Driver (Echo)
        drivers["TEST"] = object : ICapabilityDriver {
            override fun execute(request: JSONObject): JSONObject {
                Log.i("RoninKernel_Native", "L4 Diagnostic: Pipeline Test Driver Executed.")
                return JSONObject().put("success", true).put("message", "Pipeline Clear: OK")
            }
        }
        
        // v9.0: Contacts Driver (Delegated to UI for better UX/Fuzzy matching)
        drivers["CONTACTS"] = object : ICapabilityDriver {
            override fun execute(request: JSONObject): JSONObject {
                val payload = request.optString("payload", "")
                val res = executeAgentTool("CONTACTS", payload)
                return JSONObject().apply {
                    put("success", !res.startsWith("Error"))
                    put("message", res)
                    // If successful, the message is the numeric phone number
                    if (!res.startsWith("Error")) put("phone_number", res)
                }
            }
        }
    }

    @Keep
    @Suppress("unused")
    fun storeNote(title: String, content: String, tags: String): Boolean = if (isLibLoaded) storeNoteNative(title, content, tags) else false

    @Keep
    @Suppress("unused")
    fun updateBelief(key: String, value: String, confidence: Float): Boolean = if (isLibLoaded) updateBeliefNative(key, value, confidence) else false

    fun seedCapabilitiesIfEmpty() {
        if (!isLibLoaded) return
        val existing = searchNotes("capabilities")
        if (existing.isNotEmpty()) return

        storeNote(
            "Ronin System Capabilities & Architecture",
            "I am Ronin (v2.0), a native C++20 cognitive kernel running on Android. I feature a dynamic JNI bridge, dynamic capability discovery, native DSP processing, activity perception fusion, and self-learning macro-skills compilation.",
            "capabilities, overview, architecture, help"
        )
        storeNote(
            "How to use Ronin DSP Tools",
            "I support five high-performance native DSP tools: " +
            "1. 'fft': Computes Fast Fourier Transform on float arrays to output frequencies and magnitudes (minimum size 32). " +
            "2. 'lowpass': Applies a 2nd-order Butterworth lowpass filter. " +
            "3. 'detect_peaks': Finds indices of peak values above a given threshold. " +
            "4. 'zero_crossing': Calculates zero crossing rates. " +
            "5. 'rms': Computes root-mean-square value of a signal. " +
            "Inputs/Outputs are formatted as JSON arrays.",
            "dsp, fft, lowpass, peaks, zero_crossing, rms"
        )
        storeNote(
            "Ronin Perception Engine & Sensor Fusion",
            "My Kotlin Perception Engine runs a background thread at 10Hz to analyze Accelerometer/IMU data from the device. I automatically classify states: 'phone_on_table', 'phone_in_pocket', 'walking', 'running', and 'building_vibration'. These states are logged to the 'perception_history' database table and synchronized with the native C++ 'BeliefState'.",
            "sensors, accelerometer, perception, states"
        )
        storeNote(
            "Ronin Self-Learning and Macro Skills",
            "I have a C++ SkillCompiler that analyzes successful session executions. When the same sequence of tools (e.g., capture audio -> FFT) is run successfully 100 times (or test threshold), I compile them into a virtual compound tool named 'macro_skill_<sequence>' and save it to the registry.",
            "learning, macro_skills, compilation, self_learning"
        )
        Log.i("RoninKernel_Native", "Seeded Ronin capabilities documentation notes successfully.")
    }

    @Keep
    @Suppress("unused")
    fun storeFact(entity: String, attr: String, value: String): Boolean = if (isLibLoaded) storeFactNative(entity, attr, value) else false

    @Keep
    @Suppress("unused")
    fun storeAuditLog(action: String, details: String): Boolean = if (isLibLoaded) storeAuditLogNative(action, details) else false

    @Keep
    @Suppress("unused")
    fun lookupFact(entity: String, attr: String): String = if (isLibLoaded) lookupFactNative(entity, attr) else ""

    @Keep
    @Suppress("unused")
    fun lookupVault(title: String): String = if (isLibLoaded) lookupVaultNative(title) else ""

    @Keep
    @Suppress("unused")
    fun searchNotes(query: String): Array<String> = if (isLibLoaded) searchNotesNative(query) else emptyArray()

    @Keep
    @Suppress("unused")
    fun searchEpisodes(query: String): Array<String> = if (isLibLoaded) searchEpisodesNative(query) else emptyArray()

    @Keep
    @Suppress("unused")
    fun searchFiles(query: String): Array<String> = if (isLibLoaded) searchFilesNative(query) else emptyArray()

    @Keep
    @Suppress("unused")
    fun pushSensorSamples(x: FloatArray, y: FloatArray, z: FloatArray, type: String): Boolean = 
        if (isLibLoaded) pushSensorSamplesNative(x, y, z, type) else false

    @Keep
    @Suppress("unused")
    fun getSensorAnalysis(type: String): String = 
        if (isLibLoaded) getSensorAnalysisNative(type) else "{ \"error\": \"NOT_LOADED\" }"

    @Keep
    @Suppress("unused")
    fun storeVault(title: String, encryptedBlob: String): Boolean = if (isLibLoaded) storeVaultNative(title, encryptedBlob) else false

    @Keep
    @Suppress("unused")
    fun storePrediction(goalId: String, nodeId: String, predicted: String, actual: String, error: Float): Boolean = 
        if (isLibLoaded) storePredictionNative(goalId, nodeId, predicted, actual, error) else false

    @Keep
    @Suppress("unused")
    fun applyHumanFeedback(sessionId: String, wasHelpful: Boolean) {
        if (isLibLoaded) applyHumanFeedbackNative(sessionId, wasHelpful)
    }

    @Keep
    @Suppress("unused")
    fun encryptSecret(data: String): String = securityProvider.encrypt(data)

    @Keep
    @Suppress("unused")
    fun decryptSecret(encrypted: String): String = securityProvider.decrypt(encrypted)

    /**
     * v7.0 Layer 3: JNI entry point for capability requests from C++.
     */
    @Keep
    @Suppress("unused")
    fun onCapabilityRequest(jsonStr: String): String {
        return try {
            val request = org.json.JSONObject(jsonStr)
            val capability = request.getString("capability")
            val payload = request.optString("payload", "{}")

            // v9.5: Direct UI Delegation (Deterministic Routing)
            if (capability == "MAP" || capability == "SMS" || capability == "CONTACTS" || capability == "MEMORY" || capability == "TEST" || capability == "ALARM" || capability == "CALENDAR" || capability == "FILES" || capability == "SENSOR" || capability == "MAIL") {
                val action = try { org.json.JSONObject(payload).optString("action", capability) } catch (e: Exception) { capability }
                
                // Inject request_id and session_id into payload for async HITL callbacks and policy evaluation
                val enrichedPayload = try {
                    val j = org.json.JSONObject(payload)
                    j.put("request_id", request.optString("request_id", ""))
                    j.put("session_id", request.optString("session_id", ""))
                    j.toString()
                } catch (e: Exception) { payload }

                val res = executeAgentTool(action, enrichedPayload)
                
                // v11.0: Support Async HITL
                if (res == "[ASYNC_PENDING]") {
                    return org.json.JSONObject().apply {
                        put("status", "PENDING")
                        put("success", true)
                    }.toString()
                }

                return org.json.JSONObject().apply {
                    put("success", !res.startsWith("Error"))
                    put("message", res)
                }.toString()
            }

            val driver = drivers[capability]
            val driverRes = driver?.execute(request) 
                ?: org.json.JSONObject().put("success", false).put("error", "Driver not found: $capability")
            
            // v9.5: Wrap raw driver output in a standard response object
            if (!driverRes.has("success")) {
                org.json.JSONObject().apply {
                    put("success", true)
                    put("payload", driverRes.toString())
                }.toString()
            } else {
                driverRes.toString()
            }
        } catch (e: Exception) {
            org.json.JSONObject().put("success", false).put("error", "Bridge error: ${e.message}").toString()
        }
    }

    private var inferenceService: IInferenceService? = null
    private var isServiceBound = false
    private var generationTemp = 0.7f
    private var generationTopK = 40
    private var generationTopP = 0.9f
    private var generationMaxTokens = 2048

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .writeTimeout(30, TimeUnit.SECONDS)
        .build()

    private val _inferenceFlow = MutableSharedFlow<InferencePacket>(replay = 0)
    val inferenceFlow = _inferenceFlow.asSharedFlow()

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: android.content.ComponentName?, service: IBinder?) {
            inferenceService = IInferenceService.Stub.asInterface(service)
            isServiceBound = true
            Log.i(TAG, "Inference Service Bound (Isolated Process).")
            applyGenerationConfigToService()
            if (currentModelPath.isNotEmpty()) scope.launch { loadModel(currentModelPath) }
        }
        override fun onServiceDisconnected(name: android.content.ComponentName?) {
            inferenceService = null
            isServiceBound = false
        }
    }

    private val scope = kotlinx.coroutines.CoroutineScope(Dispatchers.Main + kotlinx.coroutines.SupervisorJob())
    private var currentModelPath: String = ""

    companion object {
        private const val TAG = "RoninKernel_Native"
        private var isLibLoaded = false

        suspend fun initializeAsync() = withContext(Dispatchers.IO) {
            if (isLibLoaded) return@withContext
            try {
                System.loadLibrary("ronin_kernel")
                isLibLoaded = true
                Log.i(TAG, "Native library loaded.")
            } catch (e: Exception) { Log.e(TAG, "Native load failed.") }
        }
    }

    // --- JNI API ---
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun setEngineInstanceNative()
    private external fun getChatHistoryNative(limit: Int, offset: Int): Array<String>
    private external fun notifyModelLoadedNative(path: String)
    private external fun stopLowPriorityTasksNative()
    private external fun setSafeModeNative(enabled: Boolean)
    private external fun setPriorityNative(priority: Int)
    private external fun checkFileAccessNative(path: String): String
    private external fun getFreeRamGBNative(): Float
    private external fun processInputNative(sessionId: String, execId: String, corrId: String, input: String, systemPrompt: String): String
    private external fun cancelExecutionNative(execId: String)
    private external fun isLoadedNative(): Boolean
    private external fun notifyTrimMemoryNative(level: Int)
    private external fun getActiveModelPathNative(): String
    private external fun injectLocationNative(lat: Double, lon: Double)
    private external fun injectWorldStateNative(battery: Float, ram: Float, gps: Boolean, net: Boolean, charging: Boolean, hourOfDay: Int)
    private external fun updateSystemHealthNative(temp: Float, used: Float, total: Float): Boolean
    private external fun setOfflineModeNative(offline: Boolean)
    private external fun setPrimaryCloudProviderNative(provider: String)
    private external fun indexFilesNative(paths: Array<String>, names: Array<String>, dates: LongArray)
    private external fun submitCapabilityResponseNative(requestId: String, success: Boolean, payload: String)
    private external fun getLMKPressureNative(): Int
    private external fun updateModelRegistryNative(json: String): Boolean
    private external fun updateCloudProvidersNative(json: String): Boolean
    private external fun scanSpecificPathNative(path: String): Boolean
    private external fun isValidModelNative(path: String): Boolean
    private external fun resetContextNativeJNI()
    private external fun loadMyanmarDictionaryNative(path: String): Boolean
    private external fun reportOutcomeNative(sourceId: Int, targetId: Int, success: Boolean, risk: Int)
    private external fun reportSemanticFailureNative(execId: String, nodeId: String, failureType: Int, details: String)
    private external fun runNightlyReflectionNative()
    private external fun requestCancellationNative()
    private external fun setInferenceSilenceNative(silent: Boolean)
    private external fun storeNoteNative(title: String, content: String, tags: String): Boolean
    private external fun storeFactNative(entity: String, attr: String, value: String): Boolean
    private external fun storeAuditLogNative(action: String, details: String): Boolean
    private external fun lookupFactNative(entity: String, attr: String): String
    private external fun lookupVaultNative(title: String): String
    private external fun searchNotesNative(query: String): Array<String>
    private external fun searchEpisodesNative(query: String): Array<String>
    private external fun searchFilesNative(query: String): Array<String>
    private external fun storeVaultNative(title: String, encryptedBlob: String): Boolean
    private external fun pushSensorSamplesNative(samplesX: FloatArray, samplesY: FloatArray, samplesZ: FloatArray, sensorType: String): Boolean
    private external fun getSensorAnalysisNative(sensorType: String): String
    private external fun storePredictionNative(goalId: String, nodeId: String, predicted: String, actual: String, error: Float): Boolean
    private external fun updateBeliefNative(key: String, value: String, confidence: Float): Boolean
    private external fun applyHumanFeedbackNative(sessionId: String, wasHelpful: Boolean)

    fun submitCapabilityResponseSafe(requestId: String, success: Boolean, payload: String) {
        if (isLibLoaded) submitCapabilityResponseNative(requestId, success, payload)
    }

    fun indexFilesSafe(paths: Array<String>, names: Array<String>, dates: LongArray) {
        if (isLibLoaded) indexFilesNative(paths, names, dates)
    }

    fun injectWorldState(battery: Float, ram: Float, gps: Boolean, net: Boolean, charging: Boolean) {
        if (isLibLoaded) {
            val hourOfDay = java.util.Calendar.getInstance().get(java.util.Calendar.HOUR_OF_DAY)
            injectWorldStateNative(battery, ram, gps, net, charging, hourOfDay)
        }
    }

    enum class RiskLevel(val value: Int) {
        LOW(0), MEDIUM(1), HIGH(2), EXTREME(3)
    }

    /**
     * Internal UI helper to emit tokens to UI flow.
     */
    @Suppress("unused")
    fun pushTokenToUI(fragment: String, isFinal: Boolean) {
        // v10.2.9: Correct silence logic. Only allow final packets or unsilenced tokens.
        if (inferenceSilencedFlag && !isFinal) return
        scope.launch { _inferenceFlow.emit(InferencePacket(0, fragment, isFinal)) }
    }

    fun isNativeLibraryLoaded(): Boolean = isLibLoaded

    suspend fun initialize() = withContext(Dispatchers.IO) {
        if (!isLibLoaded) initializeAsync()
        if (isLibLoaded) {
            try {
                setEngineInstanceNative()
                initializeKernelNative(context.filesDir.absolutePath, context.applicationInfo.nativeLibraryDir, false)
            } catch (e: Exception) {}
        }
        bindInferenceService()
    }

    private fun bindInferenceService() {
        val intent = Intent(context, InferenceService::class.java)
        context.bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    suspend fun loadModel(path: String): String = withContext(Dispatchers.IO) {
        val response = JSONObject()
        if (!isLibLoaded) {
            response.put("success", false)
            response.put("error", JSONObject().put("code", "SERVICE_DISCONNECTED").put("message", "JNI library not loaded"))
            return@withContext response.toString()
        }
        val start = System.currentTimeMillis()
        while (inferenceService == null && (System.currentTimeMillis() - start) < 5000) delay(200)
        if (inferenceService == null) {
            response.put("success", false)
            response.put("error", JSONObject().put("code", "SERVICE_DISCONNECTED").put("message", "Inference Service not bound"))
            return@withContext response.toString()
        }

        applyGenerationConfigToService()
        val serviceResStr = try { 
            inferenceService?.loadModel(path) ?: JSONObject().put("success", false).put("error", JSONObject().put("code", "SERVICE_DISCONNECTED").put("message", "Inference Service returned null")).toString()
        } catch (e: Exception) { 
            JSONObject().put("success", false).put("error", JSONObject().put("code", "GENERIC_ERROR").put("message", e.message)).toString()
        }
        
        try {
            val json = JSONObject(serviceResStr)
            if (json.optBoolean("success", false)) {
                notifyModelLoadedNative(path)
                currentModelPath = path
                response.put("success", true)
            } else {
                return@withContext serviceResStr
            }
        } catch (e: Exception) {
            response.put("success", false)
            response.put("error", JSONObject().put("code", "GENERIC_ERROR").put("message", "Hydration parse error: " + e.message))
        }
        return@withContext response.toString()
    }

    suspend fun loadModelResult(path: String): BridgeResult<Boolean> {
        val jsonStr = loadModel(path)
        return try {
            val json = JSONObject(jsonStr)
            if (json.optBoolean("success", false)) {
                BridgeResult.Success(true)
            } else {
                val error = json.optJSONObject("error")
                val code = error?.optString("code") ?: "UNKNOWN"
                val message = error?.optString("message") ?: "Hydration failed"
                BridgeResult.Error(code, message)
            }
        } catch (e: Exception) {
            BridgeResult.Error("GENERIC_ERROR", e.message ?: "Failed to parse result")
        }
    }

    fun isLoaded(): Boolean {
        if (isLibLoaded) try { return isLoadedNative() } catch (e: Exception) {}
        return currentModelPath.isNotEmpty()
    }

    fun getActiveModelPath(): String {
        if (isLibLoaded) try { return getActiveModelPathNative() } catch (e: Exception) {}
        return currentModelPath
    }

    data class ProcessResult(
        val result: String, 
        val sessionId: String = "",
        val success: Boolean = true,
        val errorCode: String? = null,
        val errorMessage: String? = null
    )

    fun cancelExecution(execId: String) {
        if (isLibLoaded) {
            try { cancelExecutionNative(execId) } catch (e: Exception) { Log.e(TAG, "Cancel execution failed: ${e.message}") }
        }
    }

    suspend fun processInputAsync(input: String, systemPrompt: String = ""): ProcessResult = withContext(Dispatchers.Default) {
        if (!isLibLoaded) return@withContext ProcessResult("Error: Lib not loaded.", success = false, errorCode = "SERVICE_DISCONNECTED", errorMessage = "JNI library not loaded")

        val sessionId = "S-" + System.currentTimeMillis()
        val execId = "E-" + java.util.UUID.randomUUID().toString().substring(0, 8)
        val corrId = "C-" + java.util.UUID.randomUUID().toString()

        // v1.4 Execution Governance: Link coroutine cancellation to JNI execution
        val job = coroutineContext[kotlinx.coroutines.Job]
        job?.invokeOnCompletion { cause ->
            if (cause is kotlinx.coroutines.CancellationException) {
                Log.w(TAG, "[$sessionId | $execId | CANCEL] Kotlin coroutine cancelled. Propagating to JNI.")
                cancelExecution(execId)
            }
        }

        // v6.3: Automatic Context Optimization (Thinking Stripper)
        try {
            val history = getChatHistoryAsync(20, 0)
            if (history.size >= 12) {
                Log.i(TAG, "v6.3 Context Reset: High turn count (${history.size}). Summarizing...")
                inferenceService?.summarizeAndReset()
            } else if (history.size >= 2) {
                val cleanContext = trimHistoryToContext(history, 2500)
                Log.d(TAG, "v6.3 Hybrid History: Injected ${cleanContext.length} clean chars.")
            }
        } catch (e: Exception) { Log.w(TAG, "Context optimization failed: ${e.message}") }

        try { 
            Log.i(TAG, "[$sessionId | $execId | JNI_HOP] KOTLIN -> JNI Dispatch")
            val jsonStr = processInputNative(sessionId, execId, corrId, input, systemPrompt)
            val json = org.json.JSONObject(jsonStr)
            val success = json.optBoolean("success", true)
            val error = json.optJSONObject("error")
            ProcessResult(
                result = json.getString("result"), 
                sessionId = json.optString("session_id", sessionId),
                success = success,
                errorCode = error?.optString("code"),
                errorMessage = error?.optString("message")
            )
        } catch (e: Exception) { 
            Log.e(TAG, "Process Input failed: ${e.message}")
            ProcessResult(
                result = "Error: Kernel failure.", 
                success = false,
                errorCode = "GENERIC_ERROR",
                errorMessage = e.message
            ) 
        }
    }

    suspend fun processInputResult(input: String, systemPrompt: String = ""): BridgeResult<ProcessResult> {
        val res = processInputAsync(input, systemPrompt)
        return if (res.success) {
            BridgeResult.Success(res)
        } else {
            BridgeResult.Error(res.errorCode ?: "UNKNOWN", res.errorMessage ?: "Kernel failed")
        }
    }

    fun stopInference() {
        if (isLibLoaded) {
            // v5.4: Backgrounded cancellation to prevent UI hang
            scope.launch {
                try {
                    requestCancellationNative()
                    delay(200) // Brief grace period for SDK to acknowledge
                    inferenceService?.resetConversation()
                } catch (e: Exception) { Log.e(TAG, "Stop logic failed: ${e.message}") }
            }
        }
    }

    /**
     * Triggered by C++ HardwareBridge.
     * Synchronous across processes via AIDL streaming.
     */
    @Keep
    @Suppress("unused")
    fun runNeuralReasoning(input: String): String = runBlocking(Dispatchers.IO) {
        Log.d(TAG, "Native Trigger: Initiating Hardened AIDL Reasoning...")
        val response = JSONObject()
        
        if (inferenceService == null) {
            Log.e(TAG, "Inference Service is null or not bound. Aborting reasoning.")
            response.put("success", false)
            response.put("error", JSONObject().put("code", "SERVICE_DISCONNECTED").put("message", "Inference Service is disconnected"))
            return@runBlocking response.toString()
        }

        val fullResult = StringBuilder()
        val latch = CountDownLatch(1)
        var errorResult: String? = null
        
        try {
            inferenceService?.streamReasoning(input, object : IInferenceCallback.Stub() {
                override fun onToken(fragment: String, isFinal: Boolean) {
                    if (isFinal) {
                        latch.countDown()
                    } else {
                        fullResult.append(fragment)
                        pushTokenToUI(fragment, false)
                    }
                }
                override fun onError(message: String) {
                    errorResult = message
                    latch.countDown()
                }
            })
        } catch (e: Exception) {
            response.put("success", false)
            response.put("error", JSONObject().put("code", "SERVICE_DISCONNECTED").put("message", e.message ?: "AIDL Bridge Failed"))
            return@runBlocking response.toString()
        }
        
        val finished = latch.await(60, TimeUnit.SECONDS)
        pushTokenToUI("", true)

        if (!finished) {
            response.put("success", false)
            response.put("error", JSONObject().put("code", "TIMEOUT").put("message", "Inference timed out after 60 seconds"))
            return@runBlocking response.toString()
        }

        if (errorResult != null) {
            response.put("success", false)
            val errCode = when {
                errorResult!!.contains("hydration", ignoreCase = true) -> "HYDRATION_FAILED"
                errorResult!!.contains("model", ignoreCase = true) -> "MODEL_MISSING"
                errorResult!!.contains("cancel", ignoreCase = true) -> "CANCELLED"
                else -> "GENERIC_ERROR"
            }
            response.put("error", JSONObject().put("code", errCode).put("message", errorResult))
            return@runBlocking response.toString()
        }

        response.put("success", true)
        response.put("payload", fullResult.toString())
        return@runBlocking response.toString()
    }

    fun getLMKPressureSafe(): Int {
        if (isLibLoaded) try { return getLMKPressureNative() } catch (e: Exception) {}
        return 0
    }

    fun updateSystemHealthSafe(temp: Float, used: Float, total: Float): Boolean {
        if (isLibLoaded) try { return updateSystemHealthNative(temp, used, total) } catch (e: Exception) {}
        return false
    }

    @Keep
    @Suppress("unused")
    fun updateSystemTiers(temp: Float, used: Float, total: Float) {
        onSystemTiersUpdateCallback?.invoke(temp, used, total)
    }

    @Suppress("unused")
    fun performCloudInference(input: String, provider: String, apiKey: String): String {
        return runBlocking { performCloudInferenceAsync(input, provider, apiKey) }
    }

    suspend fun performCloudInferenceAsync(input: String, provider: String, apiKey: String): String = withContext(Dispatchers.IO) {
        val response = JSONObject()
        val key = if (apiKey.isNotEmpty()) apiKey else (getSecureApiKeyProvider?.invoke(provider)?.trim() ?: "")
        if (key.isEmpty()) {
            response.put("success", false)
            response.put("error", JSONObject().put("code", "POLICY_DENIED").put("message", "API Key missing for $provider"))
            return@withContext response.toString()
        }

        var endpoint = ""
        var modelId = ""
        try {
            val providersFile = File(context.filesDir, "config/providers.json")
            if (providersFile.exists()) {
                val providersJson = JSONArray(providersFile.readText())
                for (i in 0 until providersJson.length()) {
                    val p = providersJson.getJSONObject(i)
                    if (p.getString("name") == provider) {
                        endpoint = p.getString("endpoint")
                        modelId = p.optString("modelId", "gemini-1.5-flash-latest")
                        break
                    }
                }
            }
        } catch (e: Exception) {
            response.put("success", false)
            response.put("error", JSONObject().put("code", "GENERIC_ERROR").put("message", "Config failure: " + e.message))
            return@withContext response.toString()
        }

        if (endpoint.isEmpty()) {
            response.put("success", false)
            response.put("error", JSONObject().put("code", "POLICY_DENIED").put("message", "Endpoint missing for $provider"))
            return@withContext response.toString()
        }

        try {
            val isGemini = endpoint.contains("generativelanguage")
            val finalUrl = if (isGemini) {
                val base = endpoint.removeSuffix("/")
                if (!base.contains(":generateContent")) "$base/models/$modelId:generateContent?key=$key"
                else "$base?key=$key"
            } else endpoint

            val jsonBody = if (isGemini) {
                JSONObject().put("contents", JSONArray().put(JSONObject().put("role", "user").put("parts", JSONArray().put(JSONObject().put("text", input)))))
            } else {
                JSONObject().put("model", modelId).put("messages", JSONArray().put(JSONObject().put("role", "user").put("content", input)))
            }

            val request = Request.Builder()
                .url(finalUrl)
                .post(jsonBody.toString().toRequestBody("application/json".toMediaType()))
                .apply { if (!isGemini) header("Authorization", "Bearer $key") }
                .build()

            httpClient.newCall(request).execute().use { responseHttp ->
                if (responseHttp.isSuccessful) {
                    val body = responseHttp.body?.string() ?: ""
                    val contentText = if (isGemini) {
                        JSONObject(body).getJSONArray("candidates").getJSONObject(0).getJSONObject("content").getJSONArray("parts").getJSONObject(0).getString("text")
                    } else {
                        JSONObject(body).getJSONArray("choices").getJSONObject(0).getJSONObject("message").getString("content")
                    }
                    response.put("success", true)
                    response.put("payload", contentText)
                    response.toString()
                } else {
                    response.put("success", false)
                    response.put("error", JSONObject().put("code", "GENERIC_ERROR").put("message", "HTTP ${responseHttp.code} - ${responseHttp.message}"))
                    response.toString()
                }
            }
        } catch (e: Exception) {
            response.put("success", false)
            response.put("error", JSONObject().put("code", "GENERIC_ERROR").put("message", e.message ?: "Unknown cloud error"))
            response.toString()
        }
    }

    @Keep
    @Suppress("unused")
    fun pushKernelMessage(message: String) {
        onKernelMessageCallback?.invoke(message)
    }

    @Keep
    @Suppress("unused")
    fun getSecureApiKey(provider: String): String {
        return getSecureApiKeyProvider?.invoke(provider) ?: ""
    }

    @Keep
    @Suppress("unused")
    fun triggerHardwareAction(nodeId: Int, state: Boolean): Boolean {
        return executeHardwareActionCallback?.invoke(nodeId, state) ?: true
    }

    @Keep
    @Suppress("unused")
    fun requestHardwareData(nodeId: Int): String {
        return onRequestHardwareDataCallback?.invoke(nodeId) ?: "Error"
    }

    @Keep
    @Suppress("unused")
    fun executeAgentTool(toolName: String, paramsJson: String): String {
        val result = executeAgentToolCallback?.let { callback ->
            val paramMap = mutableMapOf<String, String>()
            try {
                if (paramsJson.isNotEmpty() && paramsJson.startsWith("{")) {
                    val json = JSONObject(paramsJson)
                    val keys = json.keys()
                    while (keys.hasNext()) {
                        val key = keys.next()
                        paramMap[key] = json.getString(key)
                    }
                }
            } catch (e: Exception) { Log.e(TAG, "Agent Tool Param Parse Error: ${e.message}") }
            
            // v7.9: REMOVED runBlocking(Main). Callback MUST handle its own threading.
            callback.invoke(toolName, paramMap)
        } ?: "Error: Tool execution callback not set."
        return result
    }

    @Keep
    @Suppress("unused")
    fun requestHITLConfirmation(intentName: String, message: String): Boolean {
        var approved = false
        val latch = CountDownLatch(1)
        
        runBlocking(Dispatchers.Main) {
            requestHITLConfirmationCallback?.invoke(intentName, message) { result ->
                approved = result
                latch.countDown()
            } ?: latch.countDown()
        }
        
        try {
            // Wait for user interaction (timeout after 2 minutes)
            latch.await(120, TimeUnit.SECONDS)
        } catch (e: Exception) {}
        
        return approved
    }

    override fun onTrimMemory(level: Int) {
        if (isLibLoaded) try { notifyTrimMemoryNative(level) } catch (e: Exception) {}
    }
    override fun onConfigurationChanged(newConfig: Configuration) {}
    override fun onLowMemory() {}
    
    // Callbacks
    var onKernelMessageCallback: ((String) -> Unit)? = null
    var getSecureApiKeyProvider: ((String) -> String)? = null
    var onRequestHardwareDataCallback: ((Int) -> String)? = null
    var executeHardwareActionCallback: ((Int, Boolean) -> Boolean)? = null
    var onSystemTiersUpdateCallback: ((Float, Float, Float) -> Unit)? = null
    var requestHITLConfirmationCallback: ((String, String, (Boolean) -> Unit) -> Unit)? = null
    var executeAgentToolCallback: ((String, Map<String, String>) -> String)? = null
    var onDevHUDUpdateCallback: ((String, String, Float, String) -> Unit)? = null

    @Keep
    @Suppress("unused")
    fun updateDevHUD(state: String, intent: String, confidence: Float, plan: String) {
        onDevHUDUpdateCallback?.invoke(state, intent, confidence, plan)
    }

    @Keep
    @Suppress("unused")
    var inferenceSilencedFlag = false

    @Keep
    @Suppress("unused")
    @JvmName("setSilentInferenceFromJNI")
    fun setSilentInference(silent: Boolean) {
        inferenceSilencedFlag = silent
    }

    fun setOfflineModeSafe(offline: Boolean) {
        if (isLibLoaded) try { setOfflineModeNative(offline) } catch (e: Exception) {}
    }

    fun loadCapabilities(path: String) {
        if (isLibLoaded) try { updateModelRegistryNative(path) } catch (e: Exception) {}
    }

    fun loadMyanmarDictionary(path: String): Boolean {
        if (isLibLoaded) try { return loadMyanmarDictionaryNative(path) } catch (e: Exception) {}
        return false
    }

    fun setPrimaryCloudProviderSafe(provider: String) {
        if (isLibLoaded) try { setPrimaryCloudProviderNative(provider) } catch (e: Exception) {}
    }

    fun updateCloudProvidersSafe(json: String): Boolean {
        try {
            val configDir = File(context.filesDir, "config")
            if (!configDir.exists()) configDir.mkdirs()
            File(configDir, "providers.json").writeText(json)
        } catch (e: Exception) {}
        return if (isLibLoaded) try { updateCloudProvidersNative(json) } catch (e: Exception) { false } else false
    }

    fun isValidModel(path: String): Boolean {
        if (isLibLoaded) try { return isValidModelNative(path) } catch (e: Exception) {}
        return true
    }

    fun updateSamplingParams(temp: Float, topK: Int, topP: Float) {
        updateGenerationConfig(temp, topK, topP, generationMaxTokens)
    }

    fun updateGenerationConfig(temp: Float, topK: Int, topP: Float, maxTokens: Int) {
        generationTemp = temp
        generationTopK = topK
        generationTopP = topP
        generationMaxTokens = maxTokens
        applyGenerationConfigToService()
    }

    private fun applyGenerationConfigToService() {
        try {
            inferenceService?.updateGenerationConfig(generationTemp, generationTopK, generationTopP, generationMaxTokens)
        } catch (e: Exception) {}
    }

    private fun trimChatHistory(history: List<Pair<String, String>>, maxChars: Int = 4000): String {
        var currentLength = 0
        val optimized = mutableListOf<String>()
        for (i in history.indices.reversed()) {
            val line = "${history[i].first}: ${history[i].second}"
            if (currentLength + line.length < maxChars) {
                optimized.add(0, line)
                currentLength += line.length
            } else break
        }
        return optimized.joinToString("\n")
    }

    fun nativeResetContext() {
        if (isLibLoaded) {
            try {
                resetContextNativeJNI() 
                inferenceService?.resetConversation() 
            } catch (e: Exception) { Log.e(TAG, "Reset failed: ${e.message}") }
        }
    }

    /**
     * v6.3: Condenses history into a clean context string, stripping [THINK] blocks.
     */
    private fun trimHistoryToContext(history: List<Pair<String, String>>, maxChars: Int = 3000): String {
        val result = StringBuilder()
        // Process latest messages first until char limit is reached
        for (i in history.indices.reversed()) {
            val sender = history[i].first
            var content = history[i].second
            
            // Strip [THINK] blocks from the history for context window efficiency
            if (content.contains("[THINK]")) {
                content = content.substringAfter("[/THINK]").substringAfter("[REPLY]").trim()
            }
            
            val entry = "$sender: $content\n"
            if (result.length + entry.length < maxChars) {
                result.insert(0, entry)
            } else break
        }
        return result.toString()
    }

    fun reportOutcome(sourceId: Int, targetId: Int, success: Boolean, risk: RiskLevel = RiskLevel.MEDIUM) {
        if (isLibLoaded) try { reportOutcomeNative(sourceId, targetId, success, risk.value) } catch (e: Exception) {}
    }

    fun reportSemanticFailure(execId: String, nodeId: String, type: Int, details: String) {
        if (isLibLoaded) try { reportSemanticFailureNative(execId, nodeId, type, details) } catch (e: Exception) {}
    }

    fun runNightlyReflection() {
        if (isLibLoaded) try { runNightlyReflectionNative() } catch (e: Exception) {}
    }

    suspend fun getChatHistoryAsync(limit: Int, offset: Int): List<Pair<String, String>> = withContext(Dispatchers.IO) {
        if (!isLibLoaded) return@withContext emptyList()
        try {
            val raw = getChatHistoryNative(limit, offset)
            val result = mutableListOf<Pair<String, String>>()
            for (i in 0 until (raw.size / 2)) result.add(raw[i * 2] to raw[i * 2 + 1])
            result
        } catch (e: Exception) { emptyList() }
    }

    suspend fun fetchAvailableModels(apiKey: String, provider: String = "Gemini"): FetchResult = withContext(Dispatchers.IO) {
        val isGemini = provider.equals("Gemini", ignoreCase = true)
        val baseUrl = if (isGemini) "https://generativelanguage.googleapis.com" else "https://openrouter.ai/api/v1"
        val endpoint = if (isGemini) "$baseUrl/v1beta/models?key=$apiKey" else "$baseUrl/models"
        
        try {
            val request = Request.Builder()
                .url(endpoint)
                .get()
                .apply { if (!isGemini) header("Authorization", "Bearer $apiKey") }
                .build()

            httpClient.newCall(request).execute().use { response ->
                if (response.isSuccessful) {
                    val body = response.body?.string() ?: ""
                    val root = JSONObject(body)
                    val models = mutableListOf<JSONObject>()
                    if (isGemini) {
                        val modelArray = root.getJSONArray("models")
                        for (i in 0 until modelArray.length()) {
                            val m = modelArray.getJSONObject(i)
                            if (m.optJSONArray("supportedGenerationMethods")?.toString()?.contains("generateContent") == true) models.add(m)
                        }
                    } else {
                        val modelArray = root.getJSONArray("data")
                        for (i in 0 until modelArray.length()) models.add(modelArray.getJSONObject(i))
                    }
                    FetchResult(models, null)
                } else {
                    FetchResult(emptyList(), "HTTP ${response.code}")
                }
            }
        } catch (e: Exception) {
            FetchResult(emptyList(), e.message)
        }
    }

    data class FetchResult(val models: List<JSONObject>, val error: String? = null)
}
