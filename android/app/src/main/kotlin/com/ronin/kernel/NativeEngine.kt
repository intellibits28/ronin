package com.ronin.kernel

import android.content.Context
import android.util.Log
import android.content.ComponentCallbacks2
import android.content.res.Configuration
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import com.google.mediapipe.tasks.genai.llminference.LlmInference
import java.io.File

/**
 * Native Engine (Phase 6.0: Hybrid Ownership)
 * Kotlin owns the LlmInference instance to avoid C++ linkage issues.
 * C++ Kernel calls back into Kotlin for neural reasoning.
 */
class NativeEngine(private val context: Context) : ComponentCallbacks2 {

    private var llmInference: LlmInference? = null
    private var currentModelPath: String = ""

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
                System.loadLibrary("ronin_kernel")
                isLibLoaded = true
                Log.i(TAG, "SUCCESS: Ronin Kernel Bridge Hydrated.")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "FATAL: Native linkage failed: ${e.message}")
            }
        }
    }

    // --- JNI API (C++ Kernel Interface) ---
    private external fun initializeKernel(filesDir: String)
    private external fun setEngineInstance()
    private external fun getChatHistory(limit: Int, offset: Int): Array<String>?
    
    external fun processInput(input: String): String
    external fun notifyTrimMemory(level: Int)
    external fun injectLocation(lat: Double, lon: Double)
    external fun updateSystemHealth(temp: Float, used: Float, total: Float): Boolean
    external fun setOfflineMode(offline: Boolean)
    external fun setPrimaryCloudProvider(provider: String)
    external fun updateModelRegistry(json: String): Boolean
    external fun updateCloudProviders(json: String): Boolean
    external fun getLMKPressure(): Int

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
     * Kotlin-Side Model Hydration (The "Fix")
     */
    suspend fun loadModel(path: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val options = LlmInference.LlmInferenceOptions.builder()
                .setModelPath(path)
                .setMaxTokens(2048)
                .setTemperature(0.7f)
                .setTopK(40)
                .build()
            
            llmInference = LlmInference.createFromOptions(context, options)
            currentModelPath = path
            Log.i(TAG, "SUCCESS: Gemma 4 Brain Hydrated via Kotlin AAR.")
            true
        } catch (e: Exception) {
            Log.e(TAG, "FAILURE: Hydration error: ${e.message}")
            false
        }
    }

    // Support legacy non-async call for UI state check
    fun isLoaded(): Boolean = llmInference != null
    fun getActiveModelPath(): String = currentModelPath

    suspend fun processInputAsync(input: String): String = withContext(Dispatchers.Default) {
        if (!isLibLoaded) return@withContext "Error: Native libraries not loaded."
        processInput(input)
    }

    suspend fun getChatHistoryAsync(limit: Int, offset: Int): List<Pair<String, String>> = withContext(Dispatchers.IO) {
        if (!isLibLoaded) return@withContext emptyList<Pair<String, String>>()
        val raw = getChatHistory(limit, offset) ?: return@withContext emptyList<Pair<String, String>>()
        val result = mutableListOf<Pair<String, String>>()
        for (i in 0 until (raw.size / 2)) {
            result.add(raw[i * 2] to raw[i * 2 + 1])
        }
        result
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

    // --- JNI Callbacks (Invoked from C++ Layer via HardwareBridge) ---

    @Suppress("unused")
    fun runNeuralReasoning(input: String): String {
        return llmInference?.generateResponse(input) ?: ""
    }

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
        if (!isVpnActive(context)) {
            Log.w(TAG, "Cloud Bridge blocked: VPN inactive.")
            return "Error: Region Restricted - Please check VPN"
        }

        var finalEndpoint = ""
        try {
            val providersFile = File(File(context.filesDir, "config"), "providers.json")
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

        if (finalEndpoint.isEmpty()) {
            finalEndpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent"
        }

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

    // --- ComponentCallbacks2 ---
    override fun onTrimMemory(level: Int) {
        if (isLibLoaded) notifyTrimMemory(level)
    }
    override fun onConfigurationChanged(newConfig: Configuration) {}
    override fun onLowMemory() {
        if (isLibLoaded) notifyTrimMemory(ComponentCallbacks2.TRIM_MEMORY_COMPLETE)
    }
}
