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
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Native Engine (Phase 4.5: Dual-Process Isolation)
 * Communicates with :inference_core process via Binder IPC.
 */
class NativeEngine(private val context: Context) : ComponentCallbacks2 {

    private val dbHelper = DatabaseHelper(context)
    private val mlKit = MLKitSkill()

    // --- LEGACY INFERENCE SERVICE (FALLBACK ONLY) ---
    private var inferenceService: IInferenceService? = null
    private var isServiceBound = false

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: android.content.ComponentName?, service: IBinder?) {
            inferenceService = IInferenceService.Stub.asInterface(service)
            isServiceBound = true
            Log.i(TAG, "Inference Service Connected (IPC Active).")
            
            try {
                service?.linkToDeath({
                    Log.e(TAG, "CRITICAL: Inference Service Process DIED.")
                    isServiceBound = false
                    inferenceService = null
                    // Trigger re-bind attempt
                    scope.launch {
                        delay(2000)
                        bindInferenceService()
                    }
                }, 0)
            } catch (e: RemoteException) {
                Log.e(TAG, "Failed to link to death: ${e.message}")
            }

            // Phase 4.5.10: Auto Re-hydration on Reconnect
            if (currentModelPath.isNotEmpty()) {
                scope.launch {
                    Log.i(TAG, "Re-hydrating previous model: $currentModelPath")
                    loadModel(currentModelPath)
                }
            }
        }

        override fun onServiceDisconnected(name: android.content.ComponentName?) {
            inferenceService = null
            isServiceBound = false
            Log.w(TAG, "Inference Service Disconnected.")
        }
    }

    private val scope = kotlinx.coroutines.CoroutineScope(Dispatchers.Main + kotlinx.coroutines.SupervisorJob())

    // --- NATIVE INFERENCE STREAM (PHASE 3) ---
    private val _inferenceFlow = MutableSharedFlow<InferencePacket>(replay = 0)
    val inferenceFlow = _inferenceFlow.asSharedFlow()
    private var pollingJob: kotlinx.coroutines.Job? = null

    private var currentModelPath: String = ""
    private var asyncLatch: CountDownLatch? = null
    private var lastFullResponse: String = ""

    companion object {
        private const val TAG = "RoninKernel_Native"
        private var isLibLoaded = false
        private var lastLoadError: String? = null

        /**
         * Safe initialization to prevent Main Thread blocking during startup.
         */
        suspend fun initializeAsync() = withContext(Dispatchers.IO) {
            if (isLibLoaded) return@withContext
            try {
                System.loadLibrary("ronin_kernel")
                isLibLoaded = true
                Log.i(TAG, "SUCCESS: Ronin Kernel Bridge Hydrated on Worker Thread.")
            } catch (e: UnsatisfiedLinkError) {
                lastLoadError = e.message
                Log.e(TAG, "FATAL: Native linkage failed: ${e.message}")
            } catch (e: Exception) {
                lastLoadError = e.message
                Log.e(TAG, "FATAL: Unexpected library load error: ${e.message}")
            }
        }
    }

    // --- JNI API ---
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun setEngineInstanceNative()
    private external fun getChatHistoryNative(limit: Int, offset: Int): Array<String>?
    private external fun notifyModelLoadedNative(path: String)
    private external fun stopLowPriorityTasksNative()
    private external fun setPriorityNative(priority: Int)
    private external fun checkFileAccessNative(path: String): String
    private external fun getFreeRamGBNative(): Float
    private external fun processInputNative(input: String): String
    private external fun notifyTrimMemoryNative(level: Int)
    private external fun injectLocationNative(lat: Double, lon: Double)
    private external fun updateSystemHealthNative(temp: Float, used: Float, total: Float): Boolean
    private external fun setOfflineModeNative(offline: Boolean)
    private external fun setPrimaryCloudProviderNative(provider: String)
    private external fun updateModelRegistryNative(json: String): Boolean
    private external fun updateCloudProvidersNative(json: String): Boolean
    private external fun getLMKPressureNative(): Int
    private external fun pollInferenceStreamNative(): InferencePacket?
    private external fun pushTokenToSHMNative(fragment: String, isFinal: Boolean): Boolean
    private external fun scanSpecificPathNative(path: String): Boolean
    private external fun isLoadedNative(): Boolean
    private external fun getActiveModelPathNative(): String
    private external fun generateEmbeddingNative(text: String): FloatArray?

    /**
     * Phase 2.1: Generate semantic embedding for Memory v2.1 bridge.
     */
    fun generateEmbedding(text: String): FloatArray? {
        if (isLibLoaded) {
            return try {
                generateEmbeddingNative(text)
            } catch (e: UnsatisfiedLinkError) {
                null
            }
        }
        return null
    }

    /**
     * Phase 4.0 Audit: Verify native side can actually read the model file.
     */
    fun checkFileAccess(path: String): String {
        return if (isLibLoaded) {
            try {
                checkFileAccessNative(path)
            } catch (e: UnsatisfiedLinkError) {
                "Error: Linkage failure"
            }
        } else "Error: Library not loaded"
    }
    
    fun injectLocationSafe(lat: Double, lon: Double) {
        if (isLibLoaded) {
            try {
                injectLocationNative(lat, lon)
            } catch (e: UnsatisfiedLinkError) {}
        }
    }

    fun updateModelRegistrySafe(json: String): Boolean {
        if (isLibLoaded) {
            return try {
                updateModelRegistryNative(json)
            } catch (e: UnsatisfiedLinkError) {
                false
            }
        }
        return false
    }

    /**
     * Terminate heavy background tasks (e.g. file indexing) to save RAM.
     */
    fun stopLowPriorityTasks() {
        if (isLibLoaded) {
            try {
                stopLowPriorityTasksNative()
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Native task management call failed: ${e.message}")
            }
        }
    }

    /**
     * Adjusts the execution priority of the kernel.
     */
    fun setPriority(priority: Int) {
        if (isLibLoaded) {
            try {
                setPriorityNative(priority)
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Native priority call failed: ${e.message}")
            }
        }
    }

    fun isNativeLibraryLoaded(): Boolean = isLibLoaded

    /**
     * Phase 2.1: Memory Model Evolution
     * Translates MM to EN, generates 384-dim embedding, and saves to persistent storage.
     */
    suspend fun storeCognitiveMemory(mmText: String, importance: Float = 1.0f) {
        val enText = mlKit.translate(mmText) ?: return
        Log.i(TAG, "Memory Bridge: Translated '$mmText' -> '$enText'")
        
        val vector = generateEmbedding(enText)
        if (vector != null) {
            Log.i(TAG, "Memory Bridge: Generated ${vector.size}-dim embedding.")
            withContext(Dispatchers.IO) {
                dbHelper.storeMemory(mmText, enText, vector, importance)
            }
        } else {
            Log.e(TAG, "Memory Bridge: Failed to generate embedding.")
        }
    }

    // --- External Call Wrappers ---

    fun setOfflineModeSafe(offline: Boolean) {
        if (isLibLoaded) {
            try {
                setOfflineModeNative(offline)
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "setOfflineMode failed: ${e.message}")
            }
        }
    }

    fun setPrimaryCloudProviderSafe(provider: String) {
        if (isLibLoaded) {
            try {
                setPrimaryCloudProviderNative(provider)
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "setPrimaryCloudProvider failed: ${e.message}")
            }
        }
    }

    fun setSafeMode(enabled: Boolean) {
        /*
        try {
            inferenceService?.setSafeMode(enabled)
        } catch (e: RemoteException) {
            Log.e(TAG, "IPC setSafeMode failed: ${e.message}")
        }
        */
    }

    fun updateCloudProvidersSafe(json: String): Boolean {
        // Phase 4.5.9: Ensure providers.json is written to internal storage
        try {
            val configDir = File(context.filesDir, "config")
            if (!configDir.exists()) configDir.mkdirs()
            File(configDir, "providers.json").writeText(json)
            Log.i(TAG, "Successfully synced cloud providers to internal storage.")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to write providers.json: ${e.message}")
        }

        if (isLibLoaded) {
            return try {
                updateCloudProvidersNative(json)
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "updateCloudProviders native call failed: ${e.message}")
                false
            }
        }
        return false
    }

    fun getLMKPressureSafe(): Int {
        if (isLibLoaded) {
            return try {
                getLMKPressureNative()
            } catch (e: UnsatisfiedLinkError) {
                0
            }
        }
        return 0
    }

    fun scanSpecificPathSafe(path: String): Boolean {
        if (isLibLoaded) {
            return try {
                scanSpecificPathNative(path)
            } catch (e: UnsatisfiedLinkError) {
                false
            }
        }
        return false
    }

    // --- Callbacks for MainActivity ---
    var onKernelMessage: ((String) -> Unit)? = null
    var getSecureApiKey: ((String) -> String)? = null
    var onRequestHardwareData: ((Int) -> String)? = null
    var executeHardwareAction: ((Int, Boolean) -> Boolean)? = null
    var onSystemTiersUpdate: ((Float, Float, Float) -> Unit)? = null

    suspend fun initialize() = withContext(Dispatchers.IO) {
        if (!isLibLoaded) initializeAsync()
        if (isLibLoaded) {
            try {
                setEngineInstanceNative()
                val libDir = context.applicationInfo.nativeLibraryDir
                initializeKernelNative(context.filesDir.absolutePath, libDir, false)
                
                // Phase 2.1: Proactive model download for translation bridge
                scope.launch {
                    mlKit.ensureModelDownloaded()
                }
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "initializeKernel failed: ${e.message}")
            }
        }
        bindInferenceService()
    }
            private fun bindInferenceService() {
            val intent = Intent(context, InferenceService::class.java)
            context.bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
            }

    /**
     * Kotlin-Side Model Hydration with Native Delegation.
     */
    suspend fun loadModel(path: String): Boolean = withContext(Dispatchers.IO) {
        if (!isLibLoaded) return@withContext false
        
        // Ensure Inference Service is bound before attempting hydration
        if (!waitForService(30000)) {
            Log.e(TAG, "ABORT: Inference Service failed to bind within 30s.")
            return@withContext false
        }

        setPriority(0) // 0 = CRITICAL
        stopLowPriorityTasks()
        Log.i(TAG, ">>> [Phase 8.0 Microkernel] Delegating Hydration to :inference_core")
        
        // 1. Load model in the isolated worker process
        val workerSuccess = try {
            inferenceService?.loadModel(path) ?: false
        } catch (e: RemoteException) {
            Log.e(TAG, "IPC loadModel failed: ${e.message}")
            false
        }

        if (!workerSuccess) {
            Log.e(TAG, "Worker failed to hydrate model at $path")
            setPriority(3) // LOW
            return@withContext false
        }

        // 2. Notify Native Proxy (Stage 4 Runtime Binding)
        val nativeSuccess = try {
            notifyModelLoadedNative(path)
            true 
        } catch (e: Exception) {
            Log.e(TAG, "Native notifyModelLoaded failed: ${e.message}")
            false
        }

        if (nativeSuccess) {
            currentModelPath = path
            setPriority(1) // 1 = HIGH
            return@withContext true
        }

        setPriority(3) // 3 = LOW
        return@withContext false
    }

    private suspend fun waitForService(timeoutMs: Long): Boolean {
        val start = System.currentTimeMillis()
        while (inferenceService == null && (System.currentTimeMillis() - start) < timeoutMs) {
            Log.w(TAG, "Waiting for Inference Service binding...")
            delay(500)
        }
        return inferenceService != null
    }

    fun isLoaded(): Boolean {
        if (isLibLoaded) {
            try {
                return isLoadedNative()
            } catch (e: UnsatisfiedLinkError) {}
        }
        return currentModelPath.isNotEmpty()
    }

    fun getActiveModelPath(): String {
        if (isLibLoaded) {
            try {
                return getActiveModelPathNative()
            } catch (e: UnsatisfiedLinkError) {}
        }
        return currentModelPath
    }

    suspend fun processInputAsync(input: String): String = withContext(Dispatchers.Default) {
        if (!isLibLoaded) {
            return@withContext "Error: Native libraries not loaded. ${lastLoadError ?: "Initialization pending or failed silently."}"
        }
        try {
            val result = processInputNative(input)

            // Phase 2.1: Memory Model v2.1 Integration
            // If it's a normal conversation (not a command), store it in cognitive memory.
            if (!input.startsWith("/") && !result.startsWith("Error:")) {
                scope.launch {
                    storeCognitiveMemory(input)
                }
            }
            
            // Phase 3: If reasoning started, trigger the polling coroutine
            if (result.contains("[SHM Active]")) {
                startInferencePolling()
            }
            
            result
        } catch (e: UnsatisfiedLinkError) {
            "Error: Native bridge disconnected."
        }
    }

    private fun startInferencePolling() {
        if (pollingJob?.isActive == true) {
            Log.d(TAG, "SHM Polling already active. Skipping restart.")
            return
        }
        // Requirement 1: Use a context that survives parent cancellation for critical polling
        pollingJob = scope.launch(Dispatchers.IO) {
            withContext(kotlinx.coroutines.NonCancellable) {
                Log.d(TAG, "Starting SHM Inference Polling (Non-Cancellable)...")
                var finalReceived = false
                try {
                    while (!finalReceived) {
                        val packet = pollInferenceStreamNative()
                        if (packet != null) {
                            _inferenceFlow.emit(packet)
                            if (packet.isFinal) {
                                finalReceived = true
                                Log.d(TAG, "SHM Stream Finalized.")
                            }
                        } else {
                            delay(16) 
                        }
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Polling Error: ${e.message}")
                }
            }
        }
    }

    fun checkFreeRamGB(): Float {
        return if (isLibLoaded) {
            try {
                getFreeRamGBNative()
            } catch (e: UnsatisfiedLinkError) { 0f }
        } else 0f
    }

    /**
     * Callback invoked by C++ Kernel for neural reasoning.
     * Proxied via Binder to :inference_core process.
     * In Microkernel mode, this also streams results to SHM.
     */
    @Suppress("unused")
    fun runNeuralReasoning(input: String): String {
        Log.d(TAG, ">>> [Microkernel] Worker Reasoning: '$input'")
        
        scope.launch {
            try {
                // Ensure Polling is active for UI to see this
                startInferencePolling()

                // Call the actual LLM Engine (InferenceService)
                // We use a streaming-compatible approach if available, but for now wrap sync response into SHM
                val response = withContext(Dispatchers.IO) {
                    inferenceService?.runReasoning(input)
                } ?: "Error: Inference Service unavailable."
                
                // Phase 4.5.1: Response Routing logic
                // If the response is a status indicator, we don't push it to SHM.
                // The actual tokens are streamed from the worker process directly.
                if (response.startsWith("Reasoning Started") || response.startsWith("Processing")) {
                    Log.i(TAG, "<<< [Microkernel] Async stream active in worker. Native polling engaged.")
                } else {
                    // It's likely an error or a short sync response
                    pushTokenToSHMNative(response, true)
                    Log.i(TAG, "<<< [Microkernel] Reasoning complete (sync/error) and pushed to SHM.")
                }
            } catch (e: Exception) {
                val errorMsg = "Error: Neural spine failure - ${e::class.java.simpleName}: ${e.message ?: "Unknown crash"}"
                Log.e(TAG, "Microkernel reasoning failed: $errorMsg")
                pushTokenToSHMNative(errorMsg, true)
            }
        }
        
        return "Reasoning Started [SHM Active]"
    }

    suspend fun getChatHistoryAsync(limit: Int, offset: Int): List<Pair<String, String>> = withContext(Dispatchers.IO) {
        if (!isLibLoaded) return@withContext emptyList<Pair<String, String>>()
        try {
            val raw = getChatHistoryNative(limit, offset) ?: return@withContext emptyList<Pair<String, String>>()

            val result = mutableListOf<Pair<String, String>>()
            for (i in 0 until (raw.size / 2)) {
                result.add(raw[i * 2] to raw[i * 2 + 1])
            }
            result
        } catch (e: UnsatisfiedLinkError) {
            emptyList<Pair<String, String>>()
        }
    }

    data class FetchResult(val models: List<JSONObject>, val error: String? = null)

    suspend fun fetchAvailableModels(apiKey: String, provider: String = "Gemini"): FetchResult = withContext(Dispatchers.IO) {
        val isGemini = provider.equals("Gemini", ignoreCase = true)
        val baseUrl = if (isGemini) "https://generativelanguage.googleapis.com" else "https://openrouter.ai/api/v1"
        var lastError: String? = null

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
                            if (m.optJSONArray("supportedGenerationMethods")?.toString()?.contains("generateContent") == true) {
                                models.add(m)
                            }
                        }
                    } else {
                        val modelArray = root.getJSONArray("data")
                        for (i in 0 until modelArray.length()) models.add(modelArray.getJSONObject(i))
                    }
                    return models
                } else {
                    val errorBody = conn.errorStream?.bufferedReader()?.use { it.readText() } ?: ""
                    lastError = "Error: [${conn.responseCode}] $errorBody"
                    if (isGemini && conn.responseCode == 404) return null // Trigger fallback
                }
            } catch (e: Exception) {
                lastError = "Fetch error ($version): ${e.message}"
                Log.e(TAG, lastError!!)
            }
            return emptyList()
        }

        if (isGemini) {
            val models = tryFetch("v1beta") ?: tryFetch("v2") ?: tryFetch("v1") ?: emptyList()
            FetchResult(models, if (models.isEmpty()) lastError else null)
        } else {
            val models = tryFetch("") ?: emptyList()
            FetchResult(models, if (models.isEmpty()) lastError else null)
        }
    }

    private fun isVpnActive(context: Context): Boolean {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
        val activeNetwork = cm.activeNetwork
        val caps = cm.getNetworkCapabilities(activeNetwork)
        return caps?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_VPN) ?: false
    }

    // --- JNI Callbacks ---
    @Suppress("unused")
    fun pushKernelMessage(message: String) {
        onKernelMessage?.invoke(message)
    }

    @Suppress("unused")
    fun getSecureApiKey(provider: String): String {
        return getSecureApiKey?.invoke(provider) ?: "" 
    }

    @Suppress("unused")
    fun triggerHardwareAction(nodeId: Int, state: Boolean): Boolean {
        return executeHardwareAction?.invoke(nodeId, state) ?: true
    }

    @Suppress("unused")
    fun requestHardwareData(nodeId: Int): String {
        return onRequestHardwareData?.invoke(nodeId) ?: "Error: Request Data Callback Null"
    }

    @Suppress("unused")
    fun performCloudInference(input: String, primaryProvider: String, passedApiKey: String): String {
        // Phase 4.5.9: Relaxed Connectivity Guard
        // Instead of hard-blocking, we log a warning. The request will fail naturally if restricted.
        if (!isVpnActive(context)) {
            Log.w(TAG, "Cloud Inference requested without active VPN. This may fail in restricted regions.")
        }

        var finalEndpoint = ""
        var modelId = ""
        try {
            val configDir = File(context.filesDir, "config")
            val providersFile = File(configDir, "providers.json")
            if (providersFile.exists()) {
                val providersJson = JSONArray(providersFile.readText())
                for (i in 0 until providersJson.length()) {
                    val p = providersJson.getJSONObject(i)
                    if (p.getString("name") == primaryProvider) {
                        finalEndpoint = p.getString("endpoint")
                        modelId = p.optString("modelId", "")
                        break
                    }
                }
            }
        } catch (e: Exception) {}
        
        if (finalEndpoint.isEmpty()) {
            return "Error: No cloud endpoint configured for $primaryProvider. Please select a model in Settings."
        }
        
        return executeSingleInference(input, primaryProvider, finalEndpoint, modelId, passedApiKey)
    }

    @Suppress("unused")
    fun updateSystemTiers(temp: Float, used: Float, total: Float) {
        onSystemTiersUpdate?.invoke(temp, used, total)
    }

    private fun executeSingleInference(input: String, provider: String, endpoint: String, modelId: String, passedApiKey: String = "", isRetry: Boolean = false): String {
        val apiKey = if (passedApiKey.isNotEmpty()) passedApiKey else (getSecureApiKey?.invoke(provider)?.trim() ?: "")
        
        if (apiKey.isEmpty()) return "Error: API Key for $provider is missing."
        
        val isGemini = endpoint.contains("generativelanguage.googleapis.com")
        
        // Phase 4.5.9: Use endpoint as provided, append key if missing
        var finalUrl = if (isGemini && !endpoint.contains("?key=")) {
            "$endpoint?key=$apiKey"
        } else endpoint

        Log.d(TAG, "Cloud Inference [Provider: $provider]. URL: $finalUrl")

        return try {
            val url = java.net.URL(finalUrl)
            val conn = url.openConnection() as java.net.HttpURLConnection
            conn.connectTimeout = 15000
            conn.requestMethod = "POST"
            conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
            
            if (!isGemini) {
                conn.setRequestProperty("Authorization", "Bearer $apiKey")
            }

            val jsonBody = if (isGemini) {
                JSONObject().put("contents", JSONArray().put(JSONObject().put("role", "user").put("parts", JSONArray().put(JSONObject().put("text", input)))))
            } else {
                JSONObject()
                    .put("model", if (modelId.isNotEmpty()) modelId else "meta-llama/llama-3.1-8b-instruct")
                    .put("messages", JSONArray().put(JSONObject().put("role", "user").put("content", input)))
            }

            conn.outputStream.use { os -> os.write(jsonBody.toString().toByteArray()) }
            if (conn.responseCode == 200) {
                val response = conn.inputStream.bufferedReader().use { it.readText() }
                if (isGemini) {
                    JSONObject(response).getJSONArray("candidates").getJSONObject(0).getJSONObject("content").getJSONArray("parts").getJSONObject(0).getString("text")
                } else {
                    JSONObject(response).getJSONArray("choices").getJSONObject(0).getJSONObject("message").getString("content")
                }
            } else if (isGemini && conn.responseCode == 404 && !isRetry) {
                // Fallback attempt for v2/v1 if v1beta failed with 404 for Gemini
                val fallbackUrl = when {
                    finalUrl.contains("v1beta") -> finalUrl.replace("v1beta", "v2")
                    finalUrl.contains("v2") -> finalUrl.replace("v2", "v1")
                    finalUrl.contains("v1") -> finalUrl.replace("v1", "v1beta") // Cycle back? Or just stop.
                    else -> finalUrl
                }
                
                if (fallbackUrl != finalUrl) {
                    Log.i(TAG, "404 detected. Retrying with fallback URL: $fallbackUrl")
                    return executeSingleInference(input, provider, fallbackUrl, modelId, passedApiKey, true)
                }
                "Error: [404] Endpoint not found even after fallback."
            } else {
                val err = conn.errorStream?.bufferedReader()?.use { it.readText() } ?: ""
                "Error: [${conn.responseCode}] $err"
            }
        } catch (e: Exception) { "Error: ${e.message}" }
    }

    fun updateSystemHealthSafe(temp: Float, used: Float, total: Float): Boolean {
        if (isLibLoaded) {
            return try {
                updateSystemHealthNative(temp, used, total)
            } catch (e: UnsatisfiedLinkError) {
                false
            }
        }
        return false
    }

    override fun onTrimMemory(level: Int) {
        if (isLibLoaded) {
            try {
                notifyTrimMemoryNative(level)
            } catch (e: UnsatisfiedLinkError) {}
            if (level >= ComponentCallbacks2.TRIM_MEMORY_MODERATE) {
                Log.w(TAG, "Aggressive Memory Trim: Halting low-priority background tasks.")
                stopLowPriorityTasks()
            }
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {}

    override fun onLowMemory() {
        if (isLibLoaded) {
            try {
                notifyTrimMemoryNative(ComponentCallbacks2.TRIM_MEMORY_COMPLETE)
            } catch (e: UnsatisfiedLinkError) {}
            stopLowPriorityTasks()
        }
    }
}
