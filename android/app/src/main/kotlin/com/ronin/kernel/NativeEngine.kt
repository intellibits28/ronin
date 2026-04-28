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

    init {
        if (isLibLoaded) {
            setEngineInstance()
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
     * Stub implementation for now as requested in re-architecture.
     */
    suspend fun fetchAvailableModels(apiKey: String): List<JSONObject> = withContext(Dispatchers.IO) {
        emptyList()
    }

    // --- JNI Callbacks (Invoked from C++ Layer) ---

    @Suppress("unused")
    fun onKernelMessage(message: String) {
        Log.d(TAG, "Kernel Message: $message")
    }

    @Suppress("unused")
    fun getApiKey(provider: String): String {
        return "" 
    }

    @Suppress("unused")
    fun triggerHardwareAction(nodeId: Int, state: Boolean): Boolean {
        Log.i(TAG, "Hardware Trigger: Node $nodeId -> $state")
        return true
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
