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
import kotlinx.coroutines.withContext
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

    private var inferenceService: IInferenceService? = null
    private var isServiceBound = false

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: android.content.ComponentName?, service: IBinder?) {
            inferenceService = IInferenceService.Stub.asInterface(service)
            isServiceBound = true
            Log.i(TAG, "Inference Service Connected (IPC Active).")
        }

        override fun onServiceDisconnected(name: android.content.ComponentName?) {
            inferenceService = null
            isServiceBound = false
            Log.w(TAG, "Inference Service Disconnected.")
        }
    }

    private var currentModelPath: String = ""
    private var asyncLatch: CountDownLatch? = null
    private var lastFullResponse: String = ""

    companion object {
        private const val TAG = "RoninKernel_Native"
        private var isLibLoaded = false

        /**
         * Safe initialization to prevent Main Thread blocking during startup.
         */
        suspend fun initializeAsync() = withContext(Dispatchers.IO) {
            if (isLibLoaded) return@withContext
            try {
                System.loadLibrary("llm_inference_engine_jni")
                System.loadLibrary("ronin_kernel")
                isLibLoaded = true
                Log.i(TAG, "SUCCESS: Ronin Kernel Bridge Hydrated on Worker Thread.")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "FATAL: Native linkage failed: ${e.message}")
            }
        }
    }

    // --- JNI API ---
    private external fun initializeKernel(filesDir: String)
    private external fun setEngineInstance()
    private external fun getChatHistory(limit: Int, offset: Int): Array<String>?
    private external fun notifyModelLoaded(path: String)
    private external fun stopLowPriorityTasksNative()
    private external fun setPriorityNative(priority: Int)
    private external fun checkFileAccessNative(path: String): String
    private external fun getFreeRamGBNative(): Float
    private external fun processInput(input: String): String
    private external fun notifyTrimMemory(level: Int)
    private external fun injectLocation(lat: Double, lon: Double)
    private external fun updateSystemHealth(temp: Float, used: Float, total: Float): Boolean
    private external fun setOfflineMode(offline: Boolean)
    private external fun setPrimaryCloudProvider(provider: String)
    private external fun updateModelRegistry(json: String): Boolean
    private external fun updateCloudProviders(json: String): Boolean
    private external fun getLMKPressure(): Int

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
                injectLocation(lat, lon)
            } catch (e: UnsatisfiedLinkError) {}
        }
    }

    fun updateModelRegistrySafe(json: String): Boolean {
        if (isLibLoaded) {
            return try {
                updateModelRegistry(json)
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

    // --- External Call Wrappers ---

    fun setOfflineModeSafe(offline: Boolean) {
        if (isLibLoaded) {
            try {
                setOfflineMode(offline)
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "setOfflineMode failed: ${e.message}")
            }
        }
    }

    fun setPrimaryCloudProviderSafe(provider: String) {
        if (isLibLoaded) {
            try {
                setPrimaryCloudProvider(provider)
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "setPrimaryCloudProvider failed: ${e.message}")
            }
        }
    }

    fun setSafeMode(enabled: Boolean) {
        try {
            inferenceService?.setSafeMode(enabled)
        } catch (e: RemoteException) {
            Log.e(TAG, "IPC setSafeMode failed: ${e.message}")
        }
    }

    fun updateCloudProvidersSafe(json: String): Boolean {
        if (isLibLoaded) {
            return try {
                updateCloudProviders(json)
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "updateCloudProviders failed: ${e.message}")
                false
            }
        }
        return false
    }

    fun getLMKPressureSafe(): Int {
        if (isLibLoaded) {
            return try {
                getLMKPressure()
            } catch (e: UnsatisfiedLinkError) {
                0
            }
        }
        return 0
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
                setEngineInstance()
                initializeKernel(context.filesDir.absolutePath)
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
     * Kotlin-Side Model Hydration with IPC Delegation.
     */
    suspend fun loadModel(path: String): Boolean = withContext(Dispatchers.IO) {
        setPriority(0) // 0 = CRITICAL
        Log.i(TAG, ">>> [Phase 4.5 IPC] Delegating Hydration to :inference_core")
        val success = try {
            inferenceService?.loadModel(path) ?: false
        } catch (e: RemoteException) {
            Log.e(TAG, "IPC loadModel failed: ${e.message}")
            false
        }
        if (success) {
            currentModelPath = path
            setPriority(1) // 1 = HIGH
            if (isLibLoaded) notifyModelLoaded(path)
            return@withContext true
        }
        setPriority(3) // 3 = LOW
        return@withContext false
    }

    fun isLoaded(): Boolean {
        return try {
            inferenceService?.isHydrated ?: false
        } catch (e: RemoteException) { false }
    }

    fun getActiveModelPath(): String {
        return try {
            inferenceService?.activeModelPath ?: currentModelPath
        } catch (e: RemoteException) { currentModelPath }
    }

    suspend fun processInputAsync(input: String): String = withContext(Dispatchers.Default) {
        if (!isLibLoaded) return@withContext "Error: Native libraries not loaded."
        try {
            processInput(input)
        } catch (e: UnsatisfiedLinkError) {
            "Error: Native bridge disconnected."
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
     */
    @Suppress("unused")
    fun runNeuralReasoning(input: String): String {
        Log.d(TAG, ">>> [Phase 4.5 IPC] Neural Reasoning Delegation: '$input'")
        val freeRam = checkFreeRamGB()
        if (freeRam < 0.5f) {
            Log.w(TAG, "Insufficient RAM (%.2f GB free). Reasoning aborted.".format(freeRam))
            return "Error: Insufficient RAM for reasoning."
        }
        return try {
            val startTime = System.currentTimeMillis()
            val response = inferenceService?.runReasoning(input) ?: "Error: Inference Service unavailable."
            val duration = System.currentTimeMillis() - startTime
            Log.i(TAG, "<<< [Phase 4.5 IPC] Neural Response Received in ${duration}ms")
            response
        } catch (e: RemoteException) {
            Log.e(TAG, "IPC reasoning failed: ${e.message}")
            "Error: IPC failure - ${e.message}"
        }
    }

    suspend fun getChatHistoryAsync(limit: Int, offset: Int): List<Pair<String, String>> = withContext(Dispatchers.IO) {
        if (!isLibLoaded) return@withContext emptyList<Pair<String, String>>()
        try {
            val raw = getChatHistory(limit, offset) ?: return@withContext emptyList<Pair<String, String>>()
            val result = mutableListOf<Pair<String, String>>()
            for (i in 0 until (raw.size / 2)) {
                result.add(raw[i * 2] to raw[i * 2 + 1])
            }
            result
        } catch (e: UnsatisfiedLinkError) {
            emptyList<Pair<String, String>>()
        }
    }

    suspend fun fetchAvailableModels(apiKey: String): List<JSONObject> = withContext(Dispatchers.IO) {
        val endpoint = "https://generativelanguage.googleapis.com/v1beta/models?key=$apiKey"
        val models = mutableListOf<JSONObject>()
        try {
            val url = java.net.URL(endpoint)
            val conn = url.openConnection() as java.net.HttpURLConnection
            conn.requestMethod = "GET"
            if (conn.responseCode == 200) {
                val response = conn.inputStream.bufferedReader().use { it.readText() }
                val root = JSONObject(response)
                val modelArray = root.getJSONArray("models")
                for (i in 0 until modelArray.length()) {
                    val m = modelArray.getJSONObject(i)
                    if (m.getJSONArray("supportedGenerationMethods").toString().contains("generateContent")) {
                        models.add(m)
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Network error: ${e.message}")
        }
        models
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
    fun performCloudInference(input: String, primaryProvider: String): String {
        if (!isVpnActive(context)) return "Error: Region Restricted - Please check VPN"
        var finalEndpoint = ""
        try {
            val configDir = File(context.filesDir, "config")
            val providersFile = File(configDir, "providers.json")
            if (providersFile.exists()) {
                val providersJson = JSONArray(providersFile.readText())
                for (i in 0 until providersJson.length()) {
                    val p = providersJson.getJSONObject(i)
                    if (p.getString("name") == primaryProvider) {
                        finalEndpoint = p.getString("endpoint")
                        break
                    }
                }
            }
        } catch (e: Exception) {}
        if (finalEndpoint.isEmpty()) finalEndpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent"
        return executeSingleInference(input, primaryProvider, finalEndpoint)
    }

    @Suppress("unused")
    fun updateSystemTiers(temp: Float, used: Float, total: Float) {
        onSystemTiersUpdate?.invoke(temp, used, total)
    }

    private fun executeSingleInference(input: String, provider: String, endpoint: String): String {
        val apiKey = getSecureApiKey?.invoke(provider)?.trim() ?: ""
        if (apiKey.isEmpty()) return "Error: API Key for $provider is missing."
        val finalUrl = if (endpoint.contains("?key=")) endpoint else "$endpoint?key=$apiKey"
        return try {
            val url = java.net.URL(finalUrl)
            val conn = url.openConnection() as java.net.HttpURLConnection
            conn.connectTimeout = 15000
            conn.requestMethod = "POST"
            conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
            val jsonBody = JSONObject().put("contents", JSONArray().put(JSONObject().put("role", "user").put("parts", JSONArray().put(JSONObject().put("text", input)))))
            conn.outputStream.use { os -> os.write(jsonBody.toString().toByteArray()) }
            if (conn.responseCode == 200) {
                val response = conn.inputStream.bufferedReader().use { it.readText() }
                JSONObject(response).getJSONArray("candidates").getJSONObject(0).getJSONObject("content").getJSONArray("parts").getJSONObject(0).getString("text")
            } else "Error: [${conn.responseCode}]"
        } catch (e: Exception) { "Error: ${e.message}" }
    }

    fun updateSystemHealthSafe(temp: Float, used: Float, total: Float): Boolean {
        if (isLibLoaded) {
            return try {
                updateSystemHealth(temp, used, total)
            } catch (e: UnsatisfiedLinkError) {
                false
            }
        }
        return false
    }

    override fun onTrimMemory(level: Int) {
        if (isLibLoaded) {
            try {
                notifyTrimMemory(level)
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
                notifyTrimMemory(ComponentCallbacks2.TRIM_MEMORY_COMPLETE)
            } catch (e: UnsatisfiedLinkError) {}
            stopLowPriorityTasks()
        }
    }
}
