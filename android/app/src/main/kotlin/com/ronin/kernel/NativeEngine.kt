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
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Native Engine (Phase 11.0: Real-time Streaming IPC)
 * Zero-SHM, AIDL-based streaming for production reliability.
 */
class NativeEngine(private val context: Context) : ComponentCallbacks2 {

    private val dbHelper = DatabaseHelper(context)
    private var inferenceService: IInferenceService? = null
    private var isServiceBound = false

    private val _inferenceFlow = MutableSharedFlow<InferencePacket>(replay = 0)
    val inferenceFlow = _inferenceFlow.asSharedFlow()

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: android.content.ComponentName?, service: IBinder?) {
            inferenceService = IInferenceService.Stub.asInterface(service)
            isServiceBound = true
            Log.i(TAG, "Inference Service Bound.")
            if (currentModelPath.isNotEmpty()) scope.launch { loadModel(currentModelPath) }
        }
        override fun onServiceDisconnected(name: android.content.ComponentName?) {
            inferenceService = null
            isServiceBound = false
        }
    }

    private val scope = kotlinx.coroutines.CoroutineScope(Dispatchers.Main + kotlinx.coroutines.SupervisorJob())
    private var currentModelPath: String = ""

    companion object {
        private const val TAG = "RoninKernel_Native"
        private var isLibLoaded = false

        suspend fun initializeAsync() = withContext(Dispatchers.IO) {
            if (isLibLoaded) return@withContext
            try {
                System.loadLibrary("ronin_kernel")
                isLibLoaded = true
                Log.i(TAG, "Native library loaded.")
            } catch (e: Exception) { Log.e(TAG, "Native load failed.") }
        }
    }

    // --- JNI API ---
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun setEngineInstanceNative()
    private external fun getChatHistoryNative(limit: Int, offset: Int): Array<String>
    private external fun notifyModelLoadedNative(path: String)
    private external fun stopLowPriorityTasksNative()
    private external fun setSafeModeNative(enabled: Boolean)
    private external fun setPriorityNative(priority: Int)
    private external fun checkFileAccessNative(path: String): String
    private external fun getFreeRamGBNative(): Float
    private external fun processInputNative(input: String): String
    private external fun isLoadedNative(): Boolean
    private external fun notifyTrimMemoryNative(level: Int)
    private external fun getActiveModelPathNative(): String
    private external fun injectLocationNative(lat: Double, lon: Double)
    private external fun updateSystemHealthNative(temp: Float, used: Float, total: Float): Boolean
    private external fun setOfflineModeNative(offline: Boolean)
    private external fun setPrimaryCloudProviderNative(provider: String)
    private external fun getLMKPressureNative(): Int
    private external fun updateModelRegistryNative(json: String): Boolean
    private external fun updateCloudProvidersNative(json: String): Boolean
    private external fun scanSpecificPathNative(path: String): Boolean
    private external fun isValidModelNative(path: String): Boolean
    private external fun nativeResetContext()
    private external fun requestCancellationNative()

    // Internal JNI helper to receive tokens from C++ and emit to UI
    @Suppress("unused")
    fun pushTokenToUI(fragment: String, isFinal: Boolean) {
        scope.launch { _inferenceFlow.emit(InferencePacket(0, fragment, isFinal)) }
    }

    fun isNativeLibraryLoaded(): Boolean = isLibLoaded

    suspend fun initialize() = withContext(Dispatchers.IO) {
        if (!isLibLoaded) initializeAsync()
        if (isLibLoaded) {
            try {
                setEngineInstanceNative()
                initializeKernelNative(context.filesDir.absolutePath, context.applicationInfo.nativeLibraryDir, false)
            } catch (e: Exception) {}
        }
        bindInferenceService()
    }

    private fun bindInferenceService() {
        val intent = Intent(context, InferenceService::class.java)
        context.bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    suspend fun loadModel(path: String): Boolean = withContext(Dispatchers.IO) {
        if (!isLibLoaded) return@withContext false
        val start = System.currentTimeMillis()
        while (inferenceService == null && (System.currentTimeMillis() - start) < 5000) delay(200)
        if (inferenceService == null) return@withContext false

        val workerSuccess = try { inferenceService?.loadModel(path) ?: false } catch (e: Exception) { false }
        if (!workerSuccess) return@withContext false

        return@withContext try {
            notifyModelLoadedNative(path)
            currentModelPath = path
            true
        } catch (e: Exception) { false }
    }

    fun isLoaded(): Boolean {
        if (isLibLoaded) try { return isLoadedNative() } catch (e: Exception) {}
        return currentModelPath.isNotEmpty()
    }

    fun getActiveModelPath(): String {
        if (isLibLoaded) try { return getActiveModelPathNative() } catch (e: Exception) {}
        return currentModelPath
    }

    suspend fun processInputAsync(input: String): String = withContext(Dispatchers.Default) {
        if (!isLibLoaded) return@withContext "Error: Lib not loaded."
        try { processInputNative(input) } catch (e: Exception) { "Error: Kernel failure." }
    }

    fun stopInference() {
        if (isLibLoaded) {
            try {
                requestCancellationNative()
                nativeResetContext()
            } catch (e: Exception) {}
        }
    }

    /**
     * Triggered by C++ for real-time streaming reasoning.
     */
    @Suppress("unused")
    fun runNeuralReasoning(input: String): String {
        Log.d(TAG, "Native Trigger: Initiating Async Streaming Reasoning...")
        
        scope.launch {
            try {
                inferenceService?.streamReasoning(input, object : IInferenceCallback.Stub() {
                    override fun onToken(fragment: String, isFinal: Boolean) {
                        // Push token back to C++ so it can be handled or forwarded
                        pushTokenToUI(fragment, isFinal)
                    }
                    override fun onError(message: String) {
                        pushTokenToUI("Error: $message", true)
                    }
                })
            } catch (e: Exception) {
                pushTokenToUI("Error: IPC Failed", true)
            }
        }
        return "Reasoning Started" // C++ expects immediate ACK
    }

    fun getLMKPressureSafe(): Int {
        if (isLibLoaded) try { return getLMKPressureNative() } catch (e: Exception) {}
        return 0
    }

    fun updateSystemHealthSafe(temp: Float, used: Float, total: Float): Boolean {
        if (isLibLoaded) try { return updateSystemHealthNative(temp, used, total) } catch (e: Exception) {}
        return false
    }

    @Suppress("unused")
    fun updateSystemTiers(temp: Float, used: Float, total: Float) {
        onSystemTiersUpdateCallback?.invoke(temp, used, total)
    }

    @Suppress("unused")
    fun performCloudInference(input: String, provider: String, apiKey: String): String {
        return "Cloud fallback pending refactor."
    }

    @Suppress("unused")
    fun pushKernelMessage(message: String) {
        onKernelMessageCallback?.invoke(message)
    }

    @Suppress("unused")
    fun getSecureApiKey(provider: String): String {
        return getSecureApiKeyProvider?.invoke(provider) ?: ""
    }

    @Suppress("unused")
    fun triggerHardwareAction(nodeId: Int, state: Boolean): Boolean {
        return executeHardwareActionCallback?.invoke(nodeId, state) ?: true
    }

    @Suppress("unused")
    fun requestHardwareData(nodeId: Int): String {
        return onRequestHardwareDataCallback?.invoke(nodeId) ?: "Error"
    }

    override fun onTrimMemory(level: Int) {
        if (isLibLoaded) try { notifyTrimMemoryNative(level) } catch (e: Exception) {}
    }
    override fun onConfigurationChanged(newConfig: Configuration) {}
    override fun onLowMemory() {}
    
    var onKernelMessageCallback: ((String) -> Unit)? = null
    var getSecureApiKeyProvider: ((String) -> String)? = null
    var onRequestHardwareDataCallback: ((Int) -> String)? = null
    var executeHardwareActionCallback: ((Int, Boolean) -> Boolean)? = null
    var onSystemTiersUpdateCallback: ((Float, Float, Float) -> Unit)? = null

    fun setOfflineModeSafe(offline: Boolean) {
        if (isLibLoaded) try { setOfflineModeNative(offline) } catch (e: Exception) {}
    }
    fun setPrimaryCloudProviderSafe(provider: String) {
        if (isLibLoaded) try { setPrimaryCloudProviderNative(provider) } catch (e: Exception) {}
    }
    fun isValidModel(path: String): Boolean {
        if (isLibLoaded) try { return isValidModelNative(path) } catch (e: Exception) {}
        return true
    }
    fun normalizeBurmese(text: String): String {
        return java.text.Normalizer.normalize(text, java.text.Normalizer.Form.NFKC)
    }
    suspend fun getChatHistoryAsync(limit: Int, offset: Int): List<Pair<String, String>> = withContext(Dispatchers.IO) {
        if (!isLibLoaded) return@withContext emptyList()
        try {
            val raw = getChatHistoryNative(limit, offset)
            val result = mutableListOf<Pair<String, String>>()
            for (i in 0 until (raw.size / 2)) result.add(raw[i * 2] to raw[i * 2 + 1])
            result
        } catch (e: Exception) { emptyList() }
    }
}
