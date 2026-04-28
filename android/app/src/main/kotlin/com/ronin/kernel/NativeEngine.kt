package com.ronin.kernel

import android.content.Context
import android.util.Log
import android.content.ComponentCallbacks2
import android.content.res.Configuration
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

/**
 * Rebuilt Ronin Native Engine.
 * Aligned with Google AI Edge / MediaPipe GenAI production patterns.
 * Provides a unified bridge for LiteRT-LM reasoning and Hardware Skills.
 */
class NativeEngine(private val context: Context) : ComponentCallbacks2 {

    companion object {
        private const val TAG = "RoninKernel_Native"
        private var isLibLoaded = false

        init {
            loadNativeLibraries()
        }

        @Synchronized
        private fun loadNativeLibraries() {
            if (isLibLoaded) return
            try {
                // Order matters: Dependencies first, then JNI bridge, then Ronin Core
                System.loadLibrary("llm_inference_engine_jni")
                System.loadLibrary("ronin_kernel")
                isLibLoaded = true
                Log.i(TAG, "SUCCESS: Native reasoning spines hydrated (LiteRT + Ronin).")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "FATAL: Native linkage failed. Ensure arm64-v8a libs are in APK: ${e.message}")
            }
        }
    }

    // --- Core JNI API (Aligned with LiteRT-LM & Ronin Spine) ---
    private external fun initializeKernel(filesDir: String)
    private external fun setEngineInstance()
    
    external fun loadModel(path: String): Boolean
    external fun processInput(input: String): String
    external fun isLoaded(): Boolean
    external fun getActiveModelPath(): String
    external fun notifyTrimMemory(level: Int)
    
    // Hardware & State Sync
    external fun injectLocation(lat: Double, lon: Double)
    external fun updateSystemHealth(temp: Float, used: Float, total: Float): Boolean
    
    // Additional JNI methods required by MainActivity
    external fun setOfflineMode(offline: Boolean)
    external fun setPrimaryCloudProvider(provider: String)
    external fun getLMKPressure(): Int
    external fun updateModelRegistry(json: String): Boolean
    external fun updateCloudProviders(json: String): Boolean
    private external fun getChatHistory(limit: Int, offset: Int): Array<String>?

    // --- Callbacks for MainActivity ---
    var onKernelMessage: ((String) -> Unit)? = null
    var getSecureApiKey: ((String) -> String)? = null
    var onRequestHardwareData: ((Int) -> String)? = null
    var executeHardwareAction: ((Int, Boolean) -> Boolean)? = null
    var onSystemTiersUpdate: ((Float, Float, Float) -> Unit)? = null

    init {
        if (isLibLoaded) {
            setEngineInstance()
        }
    }

    fun initialize() {
        if (isLibLoaded) {
            initializeKernel(context.filesDir.absolutePath)
        }
    }

    /**
     * Async Input Processing.
     * Offloads neural inference to Dispatchers.Default (High CPU/NPU load).
     */
    suspend fun processInputAsync(input: String): String = withContext(Dispatchers.Default) {
        if (!isLibLoaded) return@withContext "Error: Native libraries not loaded."
        processInput(input)
    }

    /**
     * Async Model Loading.
     * Prevents UI freeze during heavy weight mapping.
     */
    suspend fun loadModelAsync(path: String): Boolean = withContext(Dispatchers.IO) {
        if (!isLibLoaded) return@withContext false
        loadModel(path)
    }

    suspend fun getChatHistoryAsync(limit: Int, offset: Int): List<Pair<String, String>> = withContext(Dispatchers.IO) {
        if (!isLibLoaded) return@withContext emptyList<Pair<String, String>>()
        val raw = getChatHistory(limit, offset) ?: return@withContext emptyList<Pair<String, String>>()
        val result = mutableListOf<Pair<String, String>>()
        for (i in 0 until raw.size / 2) {
            result.add(raw[i * 2] to raw[i * 2 + 1])
        }
        result
    }

    /**
     * Fetch available models from Google AI Edge / MediaPipe registry.
     */
    suspend fun fetchAvailableModels(apiKey: String): List<JSONObject> = withContext(Dispatchers.IO) {
        val endpoint = "https://generativelanguage.googleapis.com/v1beta/models?key=$apiKey"
        val models = mutableListOf<JSONObject>()
        try {
            val url = java.net.URL(endpoint)
            val conn = url.openConnection() as java.net.HttpURLConnection
            conn.requestMethod = "GET"
            conn.connectTimeout = 10000
            
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
            } else {
                Log.e(TAG, "Model fetch failed: ${conn.responseCode}")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Network error during model fetch: ${e.message}")
        }
        models
    }

    private fun isVpnActive(context: Context): Boolean {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
        val activeNetwork = cm.activeNetwork
        val caps = cm.getNetworkCapabilities(activeNetwork)
        return caps?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_VPN) ?: false
    }

    @Suppress("unused")
    fun performCloudInference(input: String, primaryProvider: String): String {
        if (!isVpnActive(context)) {
            Log.w(TAG, "Cloud Bridge blocked: VPN inactive.")
            return "Error: Region Restricted - Please check VPN"
        }

        Log.i(TAG, "Cloud Bridge: Initiating request with primary: $primaryProvider")
        
        val result = executeSingleInference(input, primaryProvider)
        if (result.startsWith("Error:")) {
            pushKernelMessage("> CLOUD_FAILURE: $result")
        }
        return result
    }

    private fun executeSingleInference(input: String, provider: String): String {
        val apiKey = getSecureApiKey?.invoke(provider)?.trim() ?: ""
        if (apiKey.isEmpty()) return "Error: API Key for $provider is missing."

        val modelIdClean = if (provider.contains("Gemini")) "models/gemini-1.5-flash" else provider
        val endpoint = "https://generativelanguage.googleapis.com/v1beta/$modelIdClean:generateContent?key=$apiKey"

        return try {
            val url = java.net.URL(endpoint)
            val conn = url.openConnection() as java.net.HttpURLConnection
            conn.connectTimeout = 15000
            conn.readTimeout = 15000
            conn.requestMethod = "POST"
            conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
            
            val textPart = JSONObject().put("text", input)
            val parts = JSONArray().put(textPart)
            val contentObj = JSONObject().put("role", "user").put("parts", parts)
            val jsonBody = JSONObject().put("contents", JSONArray().put(contentObj))

            conn.outputStream.use { os ->
                os.write(jsonBody.toString().toByteArray(java.nio.charset.StandardCharsets.UTF_8))
            }

            if (conn.responseCode == 200) {
                val response = conn.inputStream.bufferedReader().use { it.readText() }
                val json = JSONObject(response)
                json.getJSONArray("candidates")
                    .getJSONObject(0)
                    .getJSONObject("content")
                    .getJSONArray("parts")
                    .getJSONObject(0)
                    .getString("text")
            } else {
                "Error: [${conn.responseCode}] ${conn.errorStream?.bufferedReader()?.use { it.readText() }}"
            }
        } catch (e: Exception) {
            "Error: ${e.message}"
        }
    }

    // --- JNI Callbacks (Invoked from C++ Layer) ---

    @Suppress("unused")
    fun pushKernelMessage(message: String) {
        Log.d(TAG, "Kernel Message: $message")
        onKernelMessage?.invoke(message)
    }

    @Suppress("unused")
    fun getApiKey(provider: String): String {
        return getSecureApiKey?.invoke(provider) ?: "" 
    }

    @Suppress("unused")
    fun triggerHardwareAction(nodeId: Int, state: Boolean): Boolean {
        Log.i(TAG, "Hardware Trigger: Node $nodeId -> $state")
        return executeHardwareAction?.invoke(nodeId, state) ?: true
    }

    // --- ComponentCallbacks2 Implementation ---

    override fun onTrimMemory(level: Int) {
        if (isLibLoaded) notifyTrimMemory(level)
    }

    override fun onConfigurationChanged(newConfig: Configuration) {}

    override fun onLowMemory() {
        if (isLibLoaded) notifyTrimMemory(ComponentCallbacks2.TRIM_MEMORY_COMPLETE)
    }
}
