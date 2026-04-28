package com.ronin.kernel

import android.content.Context
import android.util.Log
import android.content.ComponentCallbacks2
import android.content.res.Configuration

class NativeEngine(private val context: Context) : ComponentCallbacks2 {

    companion object {
        private const val TAG = "RoninKernel_Native"

        init {
            try {
                // Official MediaPipe GenAI / LiteRT-LM Loading Sequence
                System.loadLibrary("llm_inference_engine_jni")
                System.loadLibrary("ronin_kernel")
                Log.i(TAG, "Native reasoning spines hydrated successfully.")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "FATAL: Native linkage failed: ${e.message}")
            }
        }
    }

    // --- Core JNI Methods ---
    external fun initializeKernel(filesDir: String)
    external fun processInput(input: String): String
    external fun loadModel(path: String): Boolean
    external fun isLoaded(): Boolean
    external fun setOfflineMode(offline: Boolean)
    external fun setPrimaryCloudProvider(provider: String)
    external fun setEngineInstance()
    external fun injectLocation(lat: Double, lon: Double)
    external fun notifyTrimMemory(level: Int)

    // --- State & Lifecycle ---
    var onKernelMessage: ((String) -> Unit)? = null

    init {
        // Synchronize C++ with this instance for callbacks
        setEngineInstance()
    }

    fun initialize() {
        initializeKernel(context.filesDir.absolutePath)
    }

    // --- JNI Callbacks (Invoked from C++) ---
    @Suppress("unused")
    fun triggerHardwareAction(nodeId: Int, state: Boolean): Boolean {
        Log.i(TAG, "Hardware Action: Node=$nodeId, State=$state")
        // Hardware logic here (simplified for this turn)
        return true
    }

    @Suppress("unused")
    fun pushKernelMessage(message: String) {
        onKernelMessage?.invoke(message)
    }

    @Suppress("unused")
    fun getSecureApiKey(provider: String): String {
        // Return API key from safe storage
        return "" 
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
