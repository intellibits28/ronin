package com.ronin.kernel

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.launch
import java.nio.ByteBuffer
import android.content.ComponentCallbacks2
import android.content.res.Configuration
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraAccessException
import android.content.Context
import org.json.JSONObject
import org.json.JSONArray

class NativeEngine : ComponentCallbacks2 {

    companion object {
        private const val TAG = "RoninNativeEngine"

        init {
            try {
                System.loadLibrary("ronin_kernel")
                Log.i(TAG, "Ronin Kernel Native Library loaded successfully.")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Failed to load ronin_kernel library: ${e.message}")
            }
        }
    }

    private var cameraManager: CameraManager? = null
    private var isFlashlightOn = false
    private var lastUserInput = ""

    fun setCameraManager(context: Context) {
        this.cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    }

    /**
     * Initializes and links kernel components.
     */
    external fun initializeKernel(filesDir: String)

    /**
     * Native call to process intent vectors using NEON SIMD.
     * Uses DirectByteBuffer for zero-copy access.
     */
    private external fun computeSimilarity(bufferA: ByteBuffer, bufferB: ByteBuffer): Float

    /**
     * Maps a DirectByteBuffer directly to the Ronin Adaptive Checkpoint schema.
     */
    private external fun loadCheckpoint(byteBuffer: ByteBuffer): Boolean

    /**
     * Syncs Android lifecycle (0 = Background, 1 = Foreground).
     */
    private external fun updateLifecycleState(state: Int)

    /**
     * Native call to process input string via reasoning spine.
     */
    private external fun processInput(input: String): String

    /**
     * Toggles whether cloud escalation is allowed (Phase 4.4.5 Privacy Layer).
     */
    external fun setOfflineMode(offline: Boolean)

    /**
     * Checks if the native inference spine is hydrated.
     */
    external fun isLoaded(): Boolean

    /**
     * Returns the absolute path of the currently loaded model.
     */
    external fun getActiveModelPath(): String

    /**
     * Runs a 1-token benchmark and returns latency in ms.
     */
    external fun verifyModel(): Long

    /**
     * Retrieves chat history from SQLite (Kernel source of truth).
     */
    private external fun getChatHistory(): Array<String>?

    /**
     * Returns the current internal pressure score (0-100).
     */
    external fun getLMKPressure(): Int

    /**
     * Retrieves chat history from SQLite (Kernel source of truth) with pagination.
     */
    private external fun getChatHistory(limit: Int, offset: Int): Array<String>?

    // --- System Health JNI Bridges ---
    external fun updateSystemHealth(temperature: Float, ramUsedGB: Float, ramTotalGB: Float): Boolean
    external fun notifyTrimMemory(level: Int)
    external fun setEngineInstance()
    external fun setPrimaryCloudProvider(provider: String)
    external fun hydrate()
    external fun injectLocation(lat: Double, lon: Double)
    external fun loadModel(path: String): Boolean
    external fun updateModelRegistry(json: String): Boolean
    external fun updateCloudProviders(json: String): Boolean

    // --- Hardware Control JNI Callbacks ---
    var executeHardwareAction: ((Int, Boolean) -> Boolean)? = null
    var onRequestHardwareData: ((Int) -> String)? = null
    var getSecureApiKey: ((String) -> String)? = null
    var onKernelMessage: ((String) -> Unit)? = null
    var onSystemTiersUpdate: ((Float, Float, Float) -> Unit)? = null

    // Called from JNI to retrieve encrypted keys from KeyStore
    @Suppress("unused")
    fun getSecureApiKey(provider: String): String {
        Log.i(TAG, "Native request: Retrieving secure key for $provider")
        return getSecureApiKey?.invoke(provider) ?: ""
    }

    // Called from C++ ExecHandlers for state toggles
    @Suppress("unused")
    fun triggerHardwareAction(nodeId: Int, cppState: Boolean): Boolean {
        Log.i(TAG, "Native request: Triggering action for Node $nodeId (C++ Suggested State: $cppState)")

        if (nodeId == 4) {
            val input = lastUserInput
            val explicitOff = input.contains("ပိတ်") || input.contains("off") || input.contains("stop") || input.contains("disable")
            val explicitOn = input.contains("ဖွင့်") || input.contains("on") || input.contains("enable")
            
            val targetState = when {
                explicitOff -> false
                explicitOn -> true
                else -> !isFlashlightOn
            }

            if (targetState != isFlashlightOn) {
                try {
                    val manager = cameraManager
                    if (manager != null) {
                        val cameraId = manager.cameraIdList[0]
                        manager.setTorchMode(cameraId, targetState)
                        isFlashlightOn = targetState
                        return isFlashlightOn
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Camera HAL lockup prevented: ${e.message}")
                    return isFlashlightOn
                }
            } else {
                return isFlashlightOn
            }
        }

        val success = try {
            executeHardwareAction?.invoke(nodeId, cppState) ?: false
        } catch (e: Exception) {
            Log.e(TAG, "CRITICAL: Hardware actuation failed for Node $nodeId", e)
            false
        }

        return success
    }

    @Suppress("unused")
    fun requestHardwareData(nodeId: Int): String {
        return onRequestHardwareData?.invoke(nodeId) ?: "Error: Hardware data provider not ready."
    }

    @Suppress("unused")
    fun updateSystemTiers(temp: Float, used: Float, total: Float) {
        onSystemTiersUpdate?.invoke(temp, used, total)
    }

    // Phase 5.1.0: Proxy-Aware Environment Sync
    private var cloudHeadersJson: String = "{}"

    external fun setCloudEnvironment(json: String)

    private fun isVpnActive(context: Context): Boolean {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as android.net.ConnectivityManager
        val activeNetwork = cm.activeNetwork
        val caps = cm.getNetworkCapabilities(activeNetwork)
        return caps?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_VPN) ?: false
    }

    @Suppress("unused")
    fun performCloudInference(input: String, primaryProvider: String): String {
        // Requirement 3: VPN Status Check
        val context = applicationContext
        if (!isVpnActive(context)) {
            Log.w(TAG, "Cloud Bridge blocked: VPN inactive.")
            return "Error: Region Restricted - Please check VPN"
        }

        Log.i(TAG, "Cloud Bridge: Initiating request with primary: $primaryProvider")
        
        val providers = mutableListOf<String>()
        try {
            val file = java.io.File("/storage/emulated/0/Ronin/config/providers.json")
            if (file.exists()) {
                val jsonArray = JSONArray(file.readText())
                for (i in 0 until jsonArray.length()) {
                    providers.add(jsonArray.getJSONObject(i).getString("name"))
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load provider registry for fallback: ${e.message}")
        }

        val chain = mutableListOf<String>()
        if (primaryProvider.isNotEmpty()) chain.add(primaryProvider)
        providers.filter { it != primaryProvider }.forEach { chain.add(it) }
        
        if (chain.isEmpty()) return "Error: No cloud providers configured."

        var lastError = ""
        for (provider in chain) {
            Log.i(TAG, "Attempting inference with: $provider")
            val result = executeSingleInference(input, provider)
            if (!result.startsWith("Error:")) {
                return result
            }
            lastError = result
            Log.w(TAG, "Provider $provider failed, falling back...")
            pushKernelMessage("> FALLBACK: Provider $provider failed. Attempting next...")
        }

        return lastError
    }

    private fun executeSingleInference(input: String, provider: String): String {
        val apiKey = getSecureApiKey(provider).trim()
        if (apiKey.isEmpty()) return "Error: API Key for $provider is missing."

        var finalEndpoint = ""
        var modelId = "gemini-1.5-flash"

        try {
            val file = java.io.File("/storage/emulated/0/Ronin/config/providers.json")
            if (file.exists()) {
                val jsonArray = JSONArray(file.readText())
                for (i in 0 until jsonArray.length()) {
                    val obj = jsonArray.getJSONObject(i)
                    if (obj.getString("name") == provider) {
                        finalEndpoint = obj.getString("endpoint")
                        modelId = obj.getString("model_id")
                        break
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Fallback to hardened builder for $provider")
        }

        // Phase 4.9.11: Use stable v1 endpoint for guaranteed production linkage
        val endpoint = if (finalEndpoint.isEmpty() || provider.contains("Gemini")) {
            if (provider.contains("Gemini")) {
                "https://generativelanguage.googleapis.com/v1/models/$modelId:generateContent?key=$apiKey"
            } else {
                when(provider) {
                    "OpenRouter" -> "https://openrouter.ai/api/v1/chat/completions"
                    else -> finalEndpoint.ifEmpty { "Error" }
                }
            }
        } else {
            finalEndpoint
        }

        if (endpoint == "Error" || endpoint.isEmpty()) return "Error: Could not resolve endpoint for $provider"

        return try {
            val url = java.net.URL(endpoint)
            val conn = url.openConnection() as java.net.HttpURLConnection
            conn.connectTimeout = 15000
            conn.readTimeout = 15000
            conn.requestMethod = "POST"
            conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
            
            if (provider == "OpenRouter") {
                conn.setRequestProperty("Authorization", "Bearer $apiKey")
                conn.setRequestProperty("HTTP-Referer", "https://github.com/intellibits28/ronin")
                conn.setRequestProperty("X-Title", "Ronin Kernel")
            }

            val jsonBody = if (provider == "Gemini") {
                // Phase 4.9.11: Minimalist content schema for stable v1 (no explicit role required)
                val parts = JSONArray().put(JSONObject().put("text", input))
                val contents = JSONArray().put(JSONObject().put("parts", parts))
                JSONObject().put("contents", contents)
            } else {
                JSONObject()
                    .put("model", modelId)
                    .put("messages", JSONArray().put(
                        JSONObject().put("role", "user").put("content", input)
                    ))
            }

            val jsonInputString = jsonBody.toString().replace("\\/", "/")

            conn.outputStream.use { os ->
                val inputBytes = jsonInputString.toByteArray(java.nio.charset.StandardCharsets.UTF_8)
                os.write(inputBytes, 0, inputBytes.size)
            }

            val responseCode = conn.responseCode
            if (responseCode == 200) {
                val response = conn.inputStream.bufferedReader().use { it.readText() }
                try {
                    val json = JSONObject(response)
                    if (provider == "Gemini") {
                        json.getJSONArray("candidates")
                            .getJSONObject(0)
                            .getJSONObject("content")
                            .getJSONArray("parts")
                            .getJSONObject(0)
                            .getString("text")
                    } else {
                        json.getJSONArray("choices")
                            .getJSONObject(0)
                            .getJSONObject("message")
                            .getString("content")
                    }
                } catch (e: Exception) {
                    Log.w(TAG, "Cloud JSON parsing failed: ${e.message}")
                    "Error: Response parsing failed."
                }
            } else {
                val errorMsg = conn.errorStream?.bufferedReader()?.use { it.readText() } ?: "No error details"
                Log.e(TAG, "Cloud request failed ($responseCode): $errorMsg")
                "Error: Cloud request failed with code $responseCode"
            }
        } catch (e: Exception) {
            Log.e(TAG, "Cloud Bridge Fatal: ${e.message}")
            "Error: ${e.message}"
        }
    }

    @Suppress("unused")
    fun pushKernelMessage(message: String) {
        Log.i(TAG, "Kernel push: $message")
        kotlinx.coroutines.CoroutineScope(kotlinx.coroutines.Dispatchers.Main).launch {
            onKernelMessage?.invoke(message)
        }
    }

    override fun onTrimMemory(level: Int) {
        if (level >= ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL) {
            Log.w(TAG, "OS Signal: Low Memory (Level $level). Notifying Kernel.")
            notifyTrimMemory(level)
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {}
    override fun onLowMemory() {
        notifyTrimMemory(ComponentCallbacks2.TRIM_MEMORY_COMPLETE)
    }

    suspend fun getChatHistoryAsync(limit: Int, offset: Int): List<Pair<String, String>> = withContext(Dispatchers.IO) {
        val raw = getChatHistory(limit, offset) ?: return@withContext emptyList<Pair<String, String>>()
        val result = mutableListOf<Pair<String, String>>()
        for (i in 0 until raw.size / 2) {
            result.add(raw[i * 2] to raw[i * 2 + 1])
        }
        result
    }

    // Phase 5.0: Dynamic Model Fetching Layer
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
                    // Only include models that support content generation
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

    suspend fun verifyApiKey(apiKey: String): Boolean = withContext(Dispatchers.IO) {
        // Simple verification by attempting to list models
        val endpoint = "https://generativelanguage.googleapis.com/v1beta/models?key=$apiKey"
        try {
            val url = java.net.URL(endpoint)
            val conn = url.openConnection() as java.net.HttpURLConnection
            conn.requestMethod = "GET"
            conn.connectTimeout = 5000
            conn.responseCode == 200
        } catch (e: Exception) { false }
    }

    suspend fun processIntentAsync(bufferA: ByteBuffer, bufferB: ByteBuffer): Float = 
        withContext(Dispatchers.Default) {
            computeSimilarity(bufferA, bufferB)
        }

    suspend fun syncLifecycle(isForeground: Boolean) = withContext(Dispatchers.Default) {
        val state = if (isForeground) 1 else 0
        updateLifecycleState(state)
    }

    suspend fun processInputAsync(input: String): String = withContext(Dispatchers.Default) {
        lastUserInput = input 
        processInput(input)
    }

    suspend fun loadModelAsync(path: String): Boolean = withContext(Dispatchers.IO) {
        pushKernelMessage("Initiating Async Hydration...")
        loadModel(path)
    }
}
