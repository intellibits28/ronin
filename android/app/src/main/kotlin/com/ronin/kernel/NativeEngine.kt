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
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Native Engine (Phase 6.6: Async Hardening & Stateful Reasoning)
 * Implements Async-to-Sync bridging with CountDownLatch for stable SD778G+ inference.
 */
class NativeEngine(private val context: Context) : ComponentCallbacks2 {

    private var llmInference: LlmInference? = null
    private var currentModelPath: String = ""
    private var asyncLatch: CountDownLatch? = null
    private var lastFullResponse: String = ""

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
                System.loadLibrary("llm_inference_engine_jni")
                System.loadLibrary("ronin_kernel")
                isLibLoaded = true
                Log.i(TAG, "SUCCESS: Ronin Kernel Bridge Hydrated.")
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

    suspend fun loadModel(path: String): Boolean = withContext(Dispatchers.IO) {
        val gpuSuccess = tryHydrate(path, true)
        if (gpuSuccess) return@withContext true
        return@withContext tryHydrate(path, false)
    }

    private fun tryHydrate(path: String, useGpu: Boolean): Boolean {
        return try {
            // Stability Hardening: Minimal builder without listeners if they fail
            val builder = LlmInference.LlmInferenceOptions.builder()
                .setModelPath(path)
                .setMaxTokens(512)
            
            llmInference = LlmInference.createFromOptions(context, builder.build())
            currentModelPath = path
            
            if (isLibLoaded) {
                notifyModelLoaded(path)
            }
            
            Log.i(TAG, "SUCCESS: Gemma 4 Brain Hydrated.")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Hydration failed: ${e.message}")
            false
        }
    }

    fun isLoaded(): Boolean = llmInference != null
    fun getActiveModelPath(): String = currentModelPath

    suspend fun processInputAsync(input: String): String = withContext(Dispatchers.Default) {
        if (!isLibLoaded) return@withContext "Error: Native libraries not loaded."
        processInput(input)
    }

    /**
     * Callback invoked by C++ Kernel for neural reasoning.
     * Uses Synchronous generateResponse with improved template to avoid empty returns.
     */
    @Suppress("unused")
    fun runNeuralReasoning(input: String): String {
        Log.d(TAG, ">>> Neural Reasoning Requested: '$input'")
        val inference = llmInference ?: return "Error: Local reasoning spine not hydrated."
        
        // Phase 6.6: Stateful Prompt Construction
        // We wrap the input to simulate a session start as Gemma 4 LiteRT-LM expects
        val formattedPrompt = if (currentModelPath.endsWith(".litertlm")) {
            "<|turn>user\n$input<turn|>\n<|turn>model\n"
        } else {
            "<start_of_turn>user\n$input<end_of_turn>\n<start_of_turn>model\n"
        }
        
        return try {
            val startTime = System.currentTimeMillis()
            val response = inference.generateResponse(formattedPrompt)
            val duration = System.currentTimeMillis() - startTime
            
            if (response.isEmpty()) {
                Log.w(TAG, "!!! Empty response received from Gemma 4.")
                return "Error: Empty response (Session initialization failed?)"
            }

            Log.i(TAG, "<<< Neural Response SUCCESS in ${duration}ms: '$response'")
            response
        } catch (e: Exception) {
            Log.e(TAG, "Inference error: ${e.message}")
            "Error: Neural spine failure - ${e.message}"
        }
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

    override fun onTrimMemory(level: Int) {
        if (isLibLoaded) notifyTrimMemory(level)
    }
    override fun onConfigurationChanged(newConfig: Configuration) {}
    override fun onLowMemory() {
        if (isLibLoaded) notifyTrimMemory(ComponentCallbacks2.TRIM_MEMORY_COMPLETE)
    }
}
