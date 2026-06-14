package com.ronin.kernel

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.google.ai.edge.litertlm.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.annotation.Keep
import java.io.File

/**
 * Hardened v4.0 Inference Spine
 * Optimized for Myanmar text (1024 tokens) and robust multi-process JNI stability.
 */
@Keep
class InferenceService : Service() {
    private val TAG = "RoninKernel_Worker"
    private val CHANNEL_ID = "ronin_hardened_spine"
    private val NOTIFICATION_ID = 3001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var currentModelPath: String = ""
    
    private var currentTemp = 0.7f
    private var currentTopK = 40
    private var currentTopP = 0.9f
    
    private var isConversationFresh = true
    private var lastSummary: String? = null
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private val inferenceMutex = Mutex()

    // --- Native JNI Interface (Essential Only) ---
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun getFreeRamGBNative(): Float
    private external fun shutdownKernelNative()

    companion object {
        init {
            try { System.loadLibrary("ronin_kernel") } catch (e: Exception) {
                Log.e("RoninKernel", "Failed to load native library.")
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, createNotification())
        try {
            initializeKernelNative(filesDir.absolutePath, applicationInfo.nativeLibraryDir, true)
        } catch (e: Throwable) {}
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val manager = getSystemService(NotificationManager::class.java)
            manager?.createNotificationChannel(NotificationChannel(CHANNEL_ID, "Ronin Inference", NotificationManager.IMPORTANCE_LOW))
        }
    }

    private fun createNotification(): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Ronin Kernel: Hardened v3.5")
            .setContentText("Neural Spine Active (v3.5 Build)")
            .setSmallIcon(android.R.drawable.ic_menu_info_details)
            .build()
    }

    private fun resetConversationLocked() {
        synchronized(this) {
            try {
                litertConversation?.close()
                litertConversation = litertEngine?.createConversation()
                isConversationFresh = true
                Log.i(TAG, "Conversation Reset: KV Cache Pruned.")
            } catch (e: Exception) { Log.e(TAG, "Reset failed: ${e.message}") }
        }
    }

    private fun resetConversationInternal() {
        runBlocking(Dispatchers.IO) {
            inferenceMutex.withLock {
                resetConversationLocked()
            }
        }
    }

    private val binder = object : IInferenceService.Stub() {
        override fun loadModel(modelPath: String): Boolean = tryHydrate(modelPath)
        
        override fun runReasoning(input: String): String = runBlocking(Dispatchers.IO) {
            val fullResult = StringBuilder()
            try { executeInference(input).collect { token -> fullResult.append(token) } ; fullResult.toString() } 
            catch (e: Exception) { "Error: ${e.message}" }
        }

        override fun streamReasoning(input: String, callback: IInferenceCallback) {
            serviceScope.launch {
                try {
                    executeInference(input).collect { token -> callback.onToken(token, false) }
                    callback.onToken("", true)
                } catch (e: Exception) { callback.onError(e.message ?: "Inference Fault") }
            }
        }

        override fun isHydrated(): Boolean = litertEngine != null
        override fun getActiveModelPath(): String = currentModelPath
        override fun notifyTrimMemory(level: Int) { if (level >= 80) resetConversationInternal() }
        override fun setSafeMode(enabled: Boolean) {}
        override fun isLowPerformanceMode(): Boolean = false
        override fun resetConversation() { 
            lastSummary = null
            resetConversationInternal() 
        }

        override fun summarizeAndReset(): String = runBlocking(Dispatchers.IO) {
            inferenceMutex.withLock {
            val activeConv = synchronized(this@InferenceService) { litertConversation } ?: return@withLock ""
            val summaryPrompt = "[INTERNAL] Summarize our conversation concisely in 3 lines of Myanmar text. Focus on facts. No thinking tags."
            val fullResult = StringBuilder()
            try {
                activeConv.sendMessageAsync(Message.user(summaryPrompt)).collect { partial ->
                    val token = try { 
                        val method = partial.javaClass.getMethod("getText")
                        method.invoke(partial) as String 
                    } catch(e: Exception) { partial.toString() }
                    fullResult.append(token)
                }
                val summary = fullResult.toString().replace("[REPLY]", "").replace("[/REPLY]", "").trim()
                lastSummary = summary
                resetConversationLocked()
                Log.i(TAG, "Summarization Complete. Next turn will include context anchor.")
                summary
            } catch (e: Exception) { 
                Log.e(TAG, "Summarization Failed: ${e.message}")
                ""
            }
            }
        }

        override fun updateSamplingParams(temp: Float, topK: Int, topP: Float) {
            currentTemp = temp; currentTopK = topK; currentTopP = topP
        }
    }

    override fun onBind(intent: Intent?): IBinder = binder

    private fun tryHydrate(path: String): Boolean {
        if (!File(path).exists()) return false
        return runBlocking(Dispatchers.IO) {
            inferenceMutex.withLock {
                releaseResourcesLocked()
                try {
                    // Hardened v5.1: Balanced EngineConfig for SD778G+
                    // Set to 1536 to allow context for Planner + History while maintaining GPU memory stability
                    val config = EngineConfig(
                        modelPath = path, 
                        maxNumTokens = 1536
                    )
                    val engine = Engine(config)
                    engine.initialize()
                    litertEngine = engine
                    litertConversation = engine.createConversation()
                    isConversationFresh = true; currentModelPath = path
                    Log.i(TAG, "Hardened Spine Hydrated (768 Token Window): $path")
                    true
                } catch (e: Throwable) { 
                    Log.e(TAG, "Hydration Failed: ${e.message}")
                    // v1.5 Self-Healing: If hydration fails, release to prevent zombie state
                    releaseResourcesLocked()
                    false 
                }
            }
        }
    }

    private fun releaseResourcesLocked() {
        synchronized(this) {
            try { 
                litertConversation?.close(); litertConversation = null
                litertEngine?.close(); litertEngine = null 
                Log.w(TAG, "Neural resources released due to instability.")
            } catch (e: Exception) {}
        }
    }

    private fun releaseResources() {
        runBlocking(Dispatchers.IO) {
            inferenceMutex.withLock {
                releaseResourcesLocked()
            }
        }
    }

    private fun executeInference(input: String): Flow<String> = flow {
        // v1.5 Self-Healing: Check and auto-hydrate if engine was released due to instability
        if (litertEngine == null && currentModelPath.isNotEmpty()) {
            Log.i(TAG, "Spine is dry. Attempting Auto-Rehydration before inference...")
            tryHydrate(currentModelPath)
        }

        inferenceMutex.withLock {
        // v7.1: Selective Stripping Hardening
        // We MUST preserve instructions for internal system calls (Summarization, Planning).
        val isInternalCall = input.contains("[INTERNAL]") || input.contains("Task Planner") || input.contains("CORE_IDENTITY")
        
        var userPrompt = if (!isConversationFresh && input.contains("User: ") && !isInternalCall) {
            input.substringAfter("User: ").trim()
        } else {
            input
        }
        
        // Inject Summary as context anchor if turn 1
        if (isConversationFresh && lastSummary != null) {
            userPrompt = "CONTEXT_ANCHOR (Previous Conversation): ${lastSummary}\n\n$userPrompt"
            Log.d(TAG, "Injected Summary Anchor into Turn 1.")
        }
        
        // v6.1 Maximum RAM Guard: 0.8GB (Mid-range protection)
        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        if (freeRam < 0.8f && !isConversationFresh) {
            Log.w(TAG, "CRITICAL RAM ALERT (%.2fGB). Purging KV Cache.".format(freeRam))
            resetConversationLocked()
        }

        val activeConv = synchronized(this@InferenceService) { litertConversation } ?: return@withLock
        
        // Mark as no longer fresh AFTER we have decided to use/strip the wrap for THIS prompt
        val wasFresh = isConversationFresh
        isConversationFresh = false 
        
        try {
            Log.d(TAG, "Inference active (Fresh=$wasFresh). Prompt length: ${userPrompt.length}")
            activeConv.sendMessageAsync(Message.user(userPrompt)).collect { partial ->
                val token = try { 
                    val method = partial.javaClass.getMethod("getText")
                    method.invoke(partial) as String 
                } catch(e: Exception) { partial.toString() }
                emit(token)
            }
        } catch (e: Exception) {
            Log.e(TAG, "LiteRT Fault: ${e.message}")
            if (e.message?.contains("Status Code: 13") == true || e.message?.contains("failed to invoke", ignoreCase = true) == true) {
                Log.w(TAG, "Terminally unstable engine state detected (Error 13). Forcing Hard Reset...")
                releaseResourcesLocked()
                // The next call will attempt to re-hydrate if the service is still alive
            } else if (e.message?.contains("not alive", ignoreCase = true) == true || e.message?.contains("failed", ignoreCase = true) == true) {
                Log.w(TAG, "Attempting to recover from dead conversation...")
                resetConversationLocked()
            }
            throw e
        }
        }
    }.flowOn(Dispatchers.IO)

    override fun onDestroy() { super.onDestroy(); releaseResources(); try { shutdownKernelNative() } catch (e: Exception) {} }
}
