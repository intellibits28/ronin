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
                // Production AAR handles System.loadLibrary internally, 
                // but we need our kernel bridge.
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
    external fun processInput(input: String): String
    external fun notifyTrimMemory(level: Int)
    external fun injectLocation(lat: Double, lon: Double)
    external fun updateSystemHealth(temp: Float, used: Float, total: Float): Boolean
    external fun setOfflineMode(offline: Boolean)
    external fun setPrimaryCloudProvider(provider: String)
    external fun updateModelRegistry(json: String): Boolean
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
            initializeKernel(context.filesDir.absolutePath)
        }
    }

    /**
     * Kotlin-Side Model Hydration (The "Fix")
     * Bypasses C++ linker errors by using official AAR bindings.
     */
    suspend fun loadModelAsync(path: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val options = LlmInference.LlmInferenceOptions.builder()
                .setModelPath(path)
                .setMaxTokens(2048)
                .setTemperature(0.7f)
                .setTopK(40)
                .build()
            
            llmInference = LlmInference.createFromOptions(context, options)
            Log.i(TAG, "SUCCESS: Gemma 4 Brain Hydrated via Kotlin AAR.")
            true
        } catch (e: Exception) {
            Log.e(TAG, "FAILURE: Hydration error: ${e.message}")
            false
        }
    }

    fun isLoaded(): Boolean = llmInference != null

    suspend fun processInputAsync(input: String): String = withContext(Dispatchers.Default) {
        if (!isLibLoaded) return@withContext "Error: Native libraries not loaded."
        processInput(input)
    }

    /**
     * Callback invoked by C++ Kernel for neural reasoning.
     * Maps the C++ request back to the Kotlin-owned LlmInference instance.
     */
    @Suppress("unused")
    fun runNeuralReasoning(input: String): String {
        return llmInference?.generateResponse(input) ?: ""
    }

    // --- Other Callbacks ---
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
    fun updateSystemTiers(temp: Float, used: Float, total: Float) {
        onSystemTiersUpdate?.invoke(temp, used, total)
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
