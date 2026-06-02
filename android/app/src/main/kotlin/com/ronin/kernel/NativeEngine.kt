package com.ronin.kernel

import android.content.Context
import android.util.Log
import android.content.ComponentCallbacks2
import android.content.res.Configuration
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import android.os.RemoteException
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
    private external fun getLMKPressureNative(): Int
    private external fun updateModelRegistryNative(json: String): Boolean
    private external fun updateCloudProvidersNative(json: String): Boolean
    private external fun scanSpecificPathNative(path: String): Boolean
    private external fun isValidModelNative(path: String): Boolean
    private external fun resetContextNativeJNI()
    private external fun loadMyanmarDictionaryNative(path: String): Boolean
    private external fun reportOutcomeNative(sourceId: Int, targetId: Int, success: Boolean, risk: Int)
    private external fun requestCancellationNative()

    enum class RiskLevel(val value: Int) {
        LOW(0), MEDIUM(1), HIGH(2), EXTREME(3)
    }

    /**
     * Internal UI helper to emit tokens to UI flow.
     */
    @Suppress("unused")
    fun pushTokenToUI(fragment: String, isFinal: Boolean) {
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

    suspend fun processInputAsync(input: String, systemPrompt: String = ""): String = withContext(Dispatchers.Default) {
        if (!isLibLoaded) return@withContext "Error: Lib not loaded."
        
        // v6.2: Automatic Context Summarization Trigger
        try {
            val history = getChatHistoryAsync(20, 0)
            if (history.size >= 8) { // 4 turns (User+Ronin pairs)
                Log.i(TAG, "Summarization Triggered: Long history detected (${history.size} items).")
                val summary = inferenceService?.summarizeAndReset()
                if (!summary.isNullOrEmpty()) {
                    Log.i(TAG, "Context anchored with summary: ${summary.take(50)}...")
                }
            }
        } catch (e: Exception) { Log.w(TAG, "Summarization check failed: ${e.message}") }

        try { processInputNative(input, systemPrompt) } catch (e: Exception) { "Error: Kernel failure." }
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
