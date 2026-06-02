package com.ronin.kernel

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.google.ai.edge.litertlm.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
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

    private fun resetConversationInternal() {
        synchronized(this) {
            try {
                litertConversation?.close()
                litertConversation = litertEngine?.createConversation()
                isConversationFresh = true
                Log.i(TAG, "Conversation Reset: KV Cache Pruned.")
            } catch (e: Exception) { Log.e(TAG, "Reset failed: ${e.message}") }
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
            val activeConv = synchronized(this@InferenceService) { litertConversation } ?: return@runBlocking ""
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
                resetConversationInternal()
                Log.i(TAG, "Summarization Complete. Next turn will include context anchor.")
                summary
            } catch (e: Exception) { 
                Log.e(TAG, "Summarization Failed: ${e.message}")
                ""
            }
        }

        override fun updateSamplingParams(temp: Float, topK: Int, topP: Float) {
            currentTemp = temp; currentTopK = topK; currentTopP = topP
        }
    }

    override fun onBind(intent: Intent?): IBinder = binder

    private fun tryHydrate(path: String): Boolean {
        if (!File(path).exists()) return false
        releaseResources()
        return try {
            // Hardened v5.1: Stabilized EngineConfig for Mi 11 Lite
            // Note: maxSequenceLength is not supported in this SDK version's constructor.
            val config = EngineConfig(
                modelPath = path, 
                maxNumTokens = 2048
            )
            val engine = Engine(config)
            engine.initialize()
            litertEngine = engine
            litertConversation = engine.createConversation()
            isConversationFresh = true; currentModelPath = path
            Log.i(TAG, "Hardened Spine Hydrated (4K Window): $path")
            true
        } catch (e: Throwable) { Log.e(TAG, "Hydration Failed: ${e.message}"); false }
    }

    private fun releaseResources() {
        synchronized(this) {
            try { litertConversation?.close(); litertConversation = null; litertEngine?.close(); litertEngine = null } 
            catch (e: Exception) {}
        }
    }

    private fun executeInference(input: String): Flow<String> = flow {
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
        
        // v6.1 Maximum RAM Guard: 0.7GB (Pushing limits for SD778G)
        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        if (freeRam < 0.7f && !isConversationFresh) {
            Log.w(TAG, "CRITICAL RAM ALERT (%.2fGB). Purging KV Cache.".format(freeRam))
            resetConversationInternal()
            // Resetting isConversationFresh here would be too late for this loop, 
            // but the next turn will see it as fresh and re-inject Persona.
        }

        val activeConv = synchronized(this@InferenceService) { litertConversation ?: return@flow }
        
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
            throw e
        }
    }.flowOn(Dispatchers.IO)

    override fun onDestroy() { super.onDestroy(); releaseResources(); try { shutdownKernelNative() } catch (e: Exception) {} }
}
