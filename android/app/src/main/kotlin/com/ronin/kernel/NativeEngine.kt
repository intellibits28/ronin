package com.ronin.kernel

import android.content.Context
import android.util.Log
import android.content.ComponentCallbacks2
import android.content.res.Configuration
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject

class NativeEngine(private val context: Context) : ComponentCallbacks2 {

    companion object {
        private const val TAG = "RoninKernel_Native"

        init {
            try {
                System.loadLibrary("llm_inference_engine_jni")
                System.loadLibrary("ronin_kernel")
                Log.i(TAG, "Native reasoning spines hydrated successfully.")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "FATAL: Native linkage failed: ${e.message}")
            }
        }
    }

    // --- Core JNI Methods ---
    private external fun initializeKernel(filesDir: String)
    external fun processInput(input: String): String
    external fun loadModel(path: String): Boolean
    external fun isLoaded(): Boolean
    external fun setOfflineMode(offline: Boolean)
    external fun setPrimaryCloudProvider(provider: String)
    external fun setEngineInstance()
    external fun injectLocation(lat: Double, lon: Double)
    external fun notifyTrimMemory(level: Int)

    // JNI methods to match MainActivity usage
    external fun getActiveModelPath(): String
    external fun updateSystemHealth(temp: Float, used: Float, total: Float): Boolean
    external fun getLMKPressure(): Int
    external fun updateModelRegistry(json: String): Boolean
    external fun updateCloudProviders(json: String): Boolean
    private external fun getChatHistory(limit: Int, offset: Int): Array<String>?

    // --- State & Lifecycle Callbacks ---
    var onKernelMessage: ((String) -> Unit)? = null
    var getSecureApiKey: ((String) -> String)? = null
    var onRequestHardwareData: ((Int) -> String)? = null
    var executeHardwareAction: ((Int, Boolean) -> Boolean)? = null
    var onSystemTiersUpdate: ((Float, Float, Float) -> Unit)? = null

    init {
        setEngineInstance()
    }

    fun initialize() {
        initializeKernel(context.filesDir.absolutePath)
    }

    fun setCameraManager(context: Context) {
        // Placeholder for compatibility
    }

    fun hydrate() {
        // Placeholder for compatibility
    }

    suspend fun processInputAsync(input: String): String = withContext(Dispatchers.Default) {
        processInput(input)
    }

    suspend fun getChatHistoryAsync(limit: Int, offset: Int): List<Pair<String, String>> = withContext(Dispatchers.IO) {
        val raw = getChatHistory(limit, offset) ?: return@withContext emptyList<Pair<String, String>>()
        val result = mutableListOf<Pair<String, String>>()
        for (i in 0 until raw.size / 2) {
            result.add(raw[i * 2] to raw[i * 2 + 1])
        }
        result
    }

    suspend fun fetchAvailableModels(apiKey: String): List<JSONObject> = withContext(Dispatchers.IO) {
        emptyList()
    }

    // --- JNI Callbacks (Invoked from C++) ---
    @Suppress("unused")
    fun triggerHardwareAction(nodeId: Int, state: Boolean): Boolean {
        Log.i(TAG, "Hardware Action Callback: Node=$nodeId, State=$state")
        return executeHardwareAction?.invoke(nodeId, state) ?: true
    }

    @Suppress("unused")
    fun pushKernelMessage(message: String) {
        onKernelMessage?.invoke(message)
    }

    @Suppress("unused")
    fun getApiKey(provider: String): String {
        return getSecureApiKey?.invoke(provider) ?: ""
    }

    // --- ComponentCallbacks2 ---
    override fun onTrimMemory(level: Int) {
        notifyTrimMemory(level)
    }

    override fun onConfigurationChanged(newConfig: Configuration) {}
    override fun onLowMemory() {
        notifyTrimMemory(ComponentCallbacks2.TRIM_MEMORY_COMPLETE)
    }
}
