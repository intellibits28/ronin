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
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Native Engine (Phase 11.2: Hardened v3.1 Architecture)
 * Uses Dual-Process Isolation and AIDL Streaming for production reliability.
 */
class NativeEngine(private val context: Context) : ComponentCallbacks2 {

    private val dbHelper = DatabaseHelper(context)
    private var inferenceService: IInferenceService? = null
    private var isServiceBound = false

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
    private external fun requestCancellationNative()

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
        try { processInputNative(input, systemPrompt) } catch (e: Exception) { "Error: Kernel failure." }
    }

    fun stopInference() {
        if (isLibLoaded) {
            try {
                requestCancellationNative()
                nativeResetContext()
            } catch (e: Exception) {}
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
        return executeSingleInference(input, provider, apiKey)
    }

    private fun executeSingleInference(input: String, provider: String, passedApiKey: String): String {
        val apiKey = if (passedApiKey.isNotEmpty()) passedApiKey else (getSecureApiKeyProvider?.invoke(provider)?.trim() ?: "")
        if (apiKey.isEmpty()) return "Error: API Key missing for $provider."

        var endpoint = ""
        var modelId = ""
        try {
            val configDir = File(context.filesDir, "config")
            val providersFile = File(configDir, "providers.json")
            if (providersFile.exists()) {
                val providersJson = JSONArray(providersFile.readText())
                for (i in 0 until providersJson.length()) {
                    val p = providersJson.getJSONObject(i)
                    if (p.getString("name") == provider) {
                        endpoint = p.getString("endpoint")
                        modelId = p.optString("modelId", "")
                        break
                    }
                }
            }
        } catch (e: Exception) { return "Error: Config failure." }

        if (endpoint.isEmpty()) return "Error: Endpoint missing for $provider."

        return try {
            val url = java.net.URL(if (endpoint.contains("generativelanguage")) "$endpoint?key=$apiKey" else endpoint)
            val conn = url.openConnection() as java.net.HttpURLConnection
            conn.connectTimeout = 15000; conn.readTimeout = 15000
            conn.requestMethod = "POST"; conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
            if (!endpoint.contains("generativelanguage")) conn.setRequestProperty("Authorization", "Bearer $apiKey")

            val jsonBody = if (endpoint.contains("generativelanguage")) {
                JSONObject().put("contents", JSONArray().put(JSONObject().put("role", "user").put("parts", JSONArray().put(JSONObject().put("text", input)))))
            } else {
                JSONObject().put("model", if (modelId.isNotEmpty()) modelId else "meta-llama/llama-3.1-8b-instruct").put("messages", JSONArray().put(JSONObject().put("role", "user").put("content", input)))
            }

            conn.outputStream.use { it.write(jsonBody.toString().toByteArray()) }
            if (conn.responseCode == 200) {
                val response = conn.inputStream.bufferedReader().use { it.readText() }
                if (endpoint.contains("generativelanguage")) JSONObject(response).getJSONArray("candidates").getJSONObject(0).getJSONObject("content").getJSONArray("parts").getJSONObject(0).getString("text")
                else JSONObject(response).getJSONArray("choices").getJSONObject(0).getJSONObject("message").getString("content")
            } else {
                val err = conn.errorStream?.bufferedReader()?.use { it.readText() } ?: "No error response"
                "Error: Server returned code ${conn.responseCode} - $err"
            }
        } catch (e: Exception) { "Error: ${e::class.java.simpleName} - ${e.message}" }
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
    fun normalizeBurmese(text: String): String {
        return java.text.Normalizer.normalize(text, java.text.Normalizer.Form.NFKC)
    }
    fun updateSamplingParams(temp: Float, topK: Int, topP: Float) {
        try { inferenceService?.updateSamplingParams(temp, topK, topP) } catch (e: Exception) {}
    }

    /**
     * Resets the cognitive context on both Native and Worker side.
     */
    fun nativeResetContext() {
        if (isLibLoaded) {
            try {
                resetContextNativeJNI() 
                inferenceService?.resetConversation() 
            } catch (e: Exception) { Log.e(TAG, "Reset failed: ${e.message}") }
        }
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
        fun tryFetch(version: String): List<JSONObject>? {
            val endpoint = if (isGemini) "$baseUrl/$version/models?key=$apiKey" else "$baseUrl/models"
            try {
                val conn = java.net.URL(endpoint).openConnection() as java.net.HttpURLConnection
                conn.requestMethod = "GET"
                if (!isGemini) conn.setRequestProperty("Authorization", "Bearer $apiKey")
                if (conn.responseCode == 200) {
                    val response = conn.inputStream.bufferedReader().use { it.readText() }
                    val root = JSONObject(response)
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
                    return models
                }
            } catch (e: Exception) {}
            return emptyList()
        }
        val models = if (isGemini) tryFetch("v1beta") ?: emptyList() else tryFetch("") ?: emptyList()
        FetchResult(models, if (models.isEmpty()) "Fetch Failed" else null)
    }

    data class FetchResult(val models: List<JSONObject>, val error: String? = null)
}
