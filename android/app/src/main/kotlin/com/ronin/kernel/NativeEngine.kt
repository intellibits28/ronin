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
    external fun hydrate()
    external fun injectLocation(lat: Double, lon: Double)
    external fun loadModel(path: String): Boolean
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

        // RULE 1: Kotlin-Level Intent Interception for Flashlight (ID 4)
        // Bypasses C++ encoding failures and provides hardware state awareness.
        if (nodeId == 4) {
            val input = lastUserInput
            
            // Native string matching handles ZWSP and Unicode variants better than C++ byte-matching
            val explicitOff = input.contains("ပိတ်") || input.contains("off") || input.contains("stop") || input.contains("disable")
            val explicitOn = input.contains("ဖွင့်") || input.contains("on") || input.contains("enable")
            
            val targetState = when {
                explicitOff -> false
                explicitOn -> true
                else -> !isFlashlightOn // Smart Toggle if the intent is ambiguous (e.g., just "Flashlight" or "မီး")
            }

            if (targetState != isFlashlightOn) {
                try {
                    val manager = cameraManager
                    if (manager != null) {
                        val cameraId = manager.cameraIdList[0]
                        manager.setTorchMode(cameraId, targetState)
                        isFlashlightOn = targetState
                        Log.i(TAG, "Kotlin Shield: Flashlight state verified and set to $targetState")
                        return isFlashlightOn // Explicitly return state
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Camera HAL lockup prevented: ${e.message}")
                    return isFlashlightOn // Return current state on failure
                }
            } else {
                Log.i(TAG, "Kotlin Shield: Flashlight already in requested state $targetState. Ignoring.")
                return isFlashlightOn // Explicitly return existing state
            }
        }

        // Standard routing for other hardware (WiFi, BT) using C++ logic
        val success = try {
            executeHardwareAction?.invoke(nodeId, cppState) ?: false
        } catch (e: Exception) {
            Log.e(TAG, "CRITICAL: Hardware actuation failed for Node $nodeId", e)
            false
        }

        return success
    }

    // Called from C++ ExecHandlers for data retrieval
    @Suppress("unused")
    fun requestHardwareData(nodeId: Int): String {
        Log.i(TAG, "Native request: Fetching data for Node $nodeId")
        return onRequestHardwareData?.invoke(nodeId) ?: "Error: Hardware data provider not ready."
    }

    // Called from JNI for real-time stability updates
    @Suppress("unused")
    fun updateSystemTiers(temp: Float, used: Float, total: Float) {
        onSystemTiersUpdate?.invoke(temp, used, total)
    }

    // Phase 4.4.6: Cloud Bridge Activation
    // Called from JNI to perform actual HTTPS POST requests to AI providers
    @Suppress("unused")
    fun performCloudInference(input: String, provider: String): String {
        Log.i(TAG, "Cloud Bridge: Initiating request to $provider")
        val apiKey = getSecureApiKey(provider)
        if (apiKey.isEmpty()) return "Error: API Key for $provider is missing."

        // Retrieve endpoint and model_id from providers.json (conceptually)
        // For prototype, we use hardcoded templates if not found in sharedPrefs
        val endpoint = when(provider) {
            "Gemini" -> "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-pro:generateContent?key=$apiKey"
            "OpenRouter" -> "https://openrouter.ai/api/v1/chat/completions"
            else -> return "Error: Unknown provider $provider"
        }

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

            val jsonInputString = if (provider == "Gemini") {
                JSONObject().put("contents", JSONArray().put(
                    JSONObject().put("parts", JSONArray().put(
                        JSONObject().put("text", input)
                    ))
                )).toString()
            } else {
                JSONObject()
                    .put("model", "meta-llama/llama-3-70b")
                    .put("messages", JSONArray().put(
                        JSONObject().put("role", "user").put("content", input)
                    )).toString()
            }

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

    // Called from JNI for asynchronous UI updates
    @Suppress("unused")
    fun pushKernelMessage(message: String) {
        Log.i(TAG, "Kernel push: $message")
        kotlinx.coroutines.CoroutineScope(kotlinx.coroutines.Dispatchers.Main).launch {
            onKernelMessage?.invoke(message)
        }
    }

    // --- ComponentCallbacks2 Implementation ---
    override fun onTrimMemory(level: Int) {
        // TRIM_MEMORY_RUNNING_CRITICAL (15) or TRIM_MEMORY_COMPLETE (80)
        if (level >= ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL) {
            Log.w(TAG, "OS Signal: Low Memory (Level $level). Notifying Kernel.")
            notifyTrimMemory(level)
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {}
    override fun onLowMemory() {
        notifyTrimMemory(ComponentCallbacks2.TRIM_MEMORY_COMPLETE)
    }

    // --- Coroutine Wrappers ---

    suspend fun getChatHistoryAsync(limit: Int, offset: Int): List<Pair<String, String>> = withContext(Dispatchers.IO) {
        val raw = getChatHistory(limit, offset) ?: return@withContext emptyList<Pair<String, String>>()
        val result = mutableListOf<Pair<String, String>>()
        for (i in 0 until raw.size / 2) {
            result.add(raw[i * 2] to raw[i * 2 + 1])
        }
        result
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
        lastUserInput = input // Cache raw input for native callback verification
        processInput(input)
    }
}
