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
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.runBlocking
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
    fun storeFact(entity: String, attr: String, value: String): Boolean = if (isLibLoaded) storeFactNative(entity, attr, value) else false

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
    fun storeVault(title: String, encryptedBlob: String): Boolean = if (isLibLoaded) storeVaultNative(title, encryptedBlob) else false

    @Keep
    @Suppress("unused")
    fun storePrediction(goalId: String, nodeId: String, predicted: String, actual: String, error: Float): Boolean = 
        if (isLibLoaded) storePredictionNative(goalId, nodeId, predicted, actual, error) else false

    @Keep
    @Suppress("unused")
    fun injectWorldState(battery: Float, ram: Float, gps: Boolean, net: Boolean, charging: Boolean) {
        if (isLibLoaded) injectWorldStateNative(battery, ram, gps, net, charging)
    }

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
    @Suppress("unused")
    fun onCapabilityRequest(jsonStr: String): String {
        return try {
            val request = org.json.JSONObject(jsonStr)
            val capability = request.getString("capability")
            val payload = request.optString("payload", "{}")

            // v9.5: Direct UI Delegation (Deterministic Routing)
            if (capability == "MAP" || capability == "SMS" || capability == "CONTACTS" || capability == "MEMORY" || capability == "TEST" || capability == "ALARM" || capability == "CALENDAR") {
                // v10.1.22: Extract specific action if available (e.g. SAVE_FACT instead of generic MEMORY)
                val action = try { org.json.JSONObject(payload).optString("action", capability) } catch (e: Exception) { capability }
                val res = executeAgentTool(action, payload)
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
    private external fun processInputNative(input: String, systemPrompt: String): String
    private external fun isLoadedNative(): Boolean
    private external fun notifyTrimMemoryNative(level: Int)
    private external fun getActiveModelPathNative(): String
    private external fun injectLocationNative(lat: Double, lon: Double)
    private external fun updateSystemHealthNative(temp: Float, used: Float, total: Float): Boolean
    private external fun setOfflineModeNative(offline: Boolean)
    private external fun setPrimaryCloudProviderNative(provider: String)
    private external fun indexFilesNative(paths: Array<String>, names: Array<String>, dates: LongArray)

    fun indexFilesSafe(paths: Array<String>, names: Array<String>, dates: LongArray) {
        if (isLibLoaded) indexFilesNative(paths, names, dates)
    }

    private external fun getLMKPressureNative(): Int
    private external fun updateModelRegistryNative(json: String): Boolean
    private external fun updateCloudProvidersNative(json: String): Boolean
    private external fun scanSpecificPathNative(path: String): Boolean
    private external fun isValidModelNative(path: String): Boolean
    private external fun resetContextNativeJNI()
    private external fun loadMyanmarDictionaryNative(path: String): Boolean
    private external fun reportOutcomeNative(sourceId: Int, targetId: Int, success: Boolean, risk: Int)
    private external fun requestCancellationNative()
    private external fun setInferenceSilenceNative(silent: Boolean)
    private external fun storeNoteNative(title: String, content: String, tags: String): Boolean
    private external fun storeFactNative(entity: String, attr: String, value: String): Boolean
    private external fun lookupFactNative(entity: String, attr: String): String
    private external fun lookupVaultNative(title: String): String
    private external fun searchNotesNative(query: String): Array<String>
    private external fun searchEpisodesNative(query: String): Array<String>
    private external fun storeVaultNative(title: String, encryptedBlob: String): Boolean
    private external fun storePredictionNative(goalId: String, nodeId: String, predicted: String, actual: String, error: Float): Boolean
    private external fun injectWorldStateNative(battery: Float, ram: Float, gps: Boolean, net: Boolean, charging: Boolean)
    private external fun applyHumanFeedbackNative(sessionId: String, wasHelpful: Boolean)

    enum class RiskLevel(val value: Int) {
        LOW(0), MEDIUM(1), HIGH(2), EXTREME(3)
    }

    /**
     * Internal UI helper to emit tokens to UI flow.
     */
    @Suppress("unused")
    fun pushTokenToUI(fragment: String, isFinal: Boolean) {
        // v10.2.9: Correct silence logic. Only allow final packets or unsilenced tokens.
        if (silentInference && !isFinal) return
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

    suspend fun loadModel(path: String): Boolean = withContext(Dispatchers.IO) {
        if (!isLibLoaded) return@withContext false
        val start = System.currentTimeMillis()
        while (inferenceService == null && (System.currentTimeMillis() - start) < 5000) delay(200)
        if (inferenceService == null) return@withContext false

        val workerSuccess = try { inferenceService?.loadModel(path) ?: false } catch (e: Exception) { false }
        if (!workerSuccess) return@withContext false

        return@withContext try {
            notifyModelLoadedNative(path)
            currentModelPath = path
            true
        } catch (e: Exception) { false }
    }

    fun isLoaded(): Boolean {
        if (isLibLoaded) try { return isLoadedNative() } catch (e: Exception) {}
        return currentModelPath.isNotEmpty()
    }

    fun getActiveModelPath(): String {
        if (isLibLoaded) try { return getActiveModelPathNative() } catch (e: Exception) {}
        return currentModelPath
    }

    data class ProcessResult(val result: String, val sessionId: String = "")

    suspend fun processInputAsync(input: String, systemPrompt: String = ""): ProcessResult = withContext(Dispatchers.Default) {
        if (!isLibLoaded) return@withContext ProcessResult("Error: Lib not loaded.")
        
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
            val jsonStr = processInputNative(input, systemPrompt)
            val json = org.json.JSONObject(jsonStr)
            ProcessResult(json.getString("result"), json.optString("session_id", ""))
        } catch (e: Exception) { 
            Log.e(TAG, "Process Input failed: ${e.message}")
            ProcessResult("Error: Kernel failure.") 
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
    @Suppress("unused")
    fun runNeuralReasoning(input: String): String = runBlocking(Dispatchers.IO) {
        Log.d(TAG, "Native Trigger: Initiating Hardened AIDL Reasoning...")
        val fullResult = StringBuilder()
        val latch = CountDownLatch(1)
        
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
                    fullResult.append("Error: $message")
                    latch.countDown()
                }
            })
        } catch (e: Exception) {
            return@runBlocking "Error: AIDL Bridge Failed"
        }
        
        latch.await(60, TimeUnit.SECONDS)
        pushTokenToUI("", true)
        fullResult.toString()
    }

    fun getLMKPressureSafe(): Int {
        if (isLibLoaded) try { return getLMKPressureNative() } catch (e: Exception) {}
        return 0
    }

    fun updateSystemHealthSafe(temp: Float, used: Float, total: Float): Boolean {
        if (isLibLoaded) try { return updateSystemHealthNative(temp, used, total) } catch (e: Exception) {}
        return false
    }

    @Suppress("unused")
    fun updateSystemTiers(temp: Float, used: Float, total: Float) {
        onSystemTiersUpdateCallback?.invoke(temp, used, total)
    }

    @Suppress("unused")
    fun performCloudInference(input: String, provider: String, apiKey: String): String {
        return runBlocking { performCloudInferenceAsync(input, provider, apiKey) }
    }

    suspend fun performCloudInferenceAsync(input: String, provider: String, apiKey: String): String = withContext(Dispatchers.IO) {
        val key = if (apiKey.isNotEmpty()) apiKey else (getSecureApiKeyProvider?.invoke(provider)?.trim() ?: "")
        if (key.isEmpty()) return@withContext "Error: API Key missing for $provider."

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
        } catch (e: Exception) { return@withContext "Error: Config failure." }

        if (endpoint.isEmpty()) return@withContext "Error: Endpoint missing for $provider."

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

            httpClient.newCall(request).execute().use { response ->
                if (response.isSuccessful) {
                    val body = response.body?.string() ?: ""
                    if (isGemini) JSONObject(body).getJSONArray("candidates").getJSONObject(0).getJSONObject("content").getJSONArray("parts").getJSONObject(0).getString("text")
                    else JSONObject(body).getJSONArray("choices").getJSONObject(0).getJSONObject("message").getString("content")
                } else {
                    "Error: HTTP ${response.code} - ${response.message}"
                }
            }
        } catch (e: Exception) { "Error: ${e.message}" }
    }

    @Suppress("unused")
    fun pushKernelMessage(message: String) {
        onKernelMessageCallback?.invoke(message)
    }

    @Suppress("unused")
    fun getSecureApiKey(provider: String): String {
        return getSecureApiKeyProvider?.invoke(provider) ?: ""
    }

    @Suppress("unused")
    fun triggerHardwareAction(nodeId: Int, state: Boolean): Boolean {
        return executeHardwareActionCallback?.invoke(nodeId, state) ?: true
    }

    @Suppress("unused")
    fun requestHardwareData(nodeId: Int): String {
        return onRequestHardwareDataCallback?.invoke(nodeId) ?: "Error"
    }

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
    
    // v7.6: Flag to silence internal system tokens (Planning, Summarization) from UI
    @Volatile
    var silentInference = false

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
        try { inferenceService?.updateSamplingParams(temp, topK, topP) } catch (e: Exception) {}
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
