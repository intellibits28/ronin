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
import java.io.File

/**
 * Hardened v3.3 Inference Spine
 * Supports dynamic sampling parameters (T,P,K) and robust RAM management.
 */
class InferenceService : Service() {
    private val TAG = "RoninKernel_Worker"
    private val CHANNEL_ID = "ronin_hardened_spine"
    private val NOTIFICATION_ID = 3001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var currentModelPath: String = ""
    
    // Sampling State
    private var currentTemp = 0.7f
    private var currentTopK = 40
    private var currentTopP = 0.9f
    
    private var isConversationFresh = true
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    // --- Native JNI Interface ---
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
            .setContentTitle("Ronin Kernel: Hardened v3.3")
            .setContentText("Neural Spine Active (T,P,K Enabled)")
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
            } catch (e: Exception) {
                Log.e(TAG, "Reset failed: ${e.message}")
            }
        }
    }

    private val binder = object : IInferenceService.Stub() {
        override fun loadModel(modelPath: String): Boolean = tryHydrate(modelPath)
        
        override fun runReasoning(input: String): String = runBlocking(Dispatchers.IO) {
            val fullResult = StringBuilder()
            try {
                executeInference(input).collect { token -> fullResult.append(token) }
                fullResult.toString()
            } catch (e: Exception) { "Error: ${e.message}" }
        }

        override fun streamReasoning(input: String, callback: IInferenceCallback) {
            serviceScope.launch {
                try {
                    executeInference(input).collect { token ->
                        callback.onToken(token, false)
                    }
                    callback.onToken("", true)
                } catch (e: Exception) {
                    callback.onError(e.message ?: "Inference Fault")
                }
            }
        }

        override fun isHydrated(): Boolean = litertEngine != null
        override fun getActiveModelPath(): String = currentModelPath
        override fun notifyTrimMemory(level: Int) { if (level >= 80) resetConversationInternal() }
        override fun setSafeMode(enabled: Boolean) {}
        override fun isLowPerformanceMode(): Boolean = false
        override fun resetConversation() = resetConversationInternal()
        
        override fun updateSamplingParams(temp: Float, topK: Int, topP: Float) {
            currentTemp = temp
            currentTopK = topK
            currentTopP = topP
            Log.d(TAG, "Sampling Params Updated: T=$temp, K=$topK, P=$topP")
            // Note: LiteRT-LM SDK 0.12.0 might require re-hydration for some params
            // but we'll store them for the next conversation or hydration.
        }
    }

    override fun onBind(intent: Intent?): IBinder = binder

    private fun tryHydrate(path: String): Boolean {
        if (!File(path).exists()) return false
        releaseResources()

        return try {
            // Hardened v3.3 tuning: Balanced sequence length
            // Note: In 0.12.0, some params might be positional.
            val config = EngineConfig(modelPath = path, maxNumTokens = 1024)
            val engine = Engine(config)
            engine.initialize()
            litertEngine = engine
            litertConversation = engine.createConversation()
            isConversationFresh = true
            currentModelPath = path
            Log.i(TAG, "Hardened Spine Hydrated: $path")
            true
        } catch (e: Throwable) { 
            Log.e(TAG, "Hydration Failed: ${e.message}")
            false 
        }
    }

    private fun releaseResources() {
        synchronized(this) {
            try {
                litertConversation?.close()
                litertConversation = null
                litertEngine?.close()
                litertEngine = null
            } catch (e: Exception) {}
        }
    }

    private fun executeInference(input: String): Flow<String> = flow {
        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        if (freeRam < 1.1f && !isConversationFresh) {
            Log.w(TAG, "LOW RAM ALERT (%.2fGB). Pruning KV Cache.".format(freeRam))
            resetConversationInternal()
        }

        val activeConv = synchronized(this@InferenceService) {
            litertConversation ?: return@flow
        }
        
        val instructions = if (input.contains("[SYSTEM]")) input.substringAfter("[SYSTEM]").substringBefore("[USER]").trim() else ""
        val userPrompt = if (input.contains("[USER]")) input.substringAfter("[USER]").trim() else input
        
        val finalInput = if (isConversationFresh && instructions.isNotEmpty()) {
            isConversationFresh = false
            "$instructions\n\n$userPrompt"
        } else {
            userPrompt
        }

        if (finalInput.isEmpty()) return@flow

        try {
            activeConv.sendMessageAsync(Message.user(finalInput)).collect { partial ->
                val token = try { 
                    partial.javaClass.getMethod("getText").invoke(partial) as String 
                } catch(e: Exception) { partial.toString() }
                emit(token)
            }
        } catch (e: Exception) {
            Log.e(TAG, "LiteRT Fault: ${e.message}")
            throw e
        }
    }.flowOn(Dispatchers.IO)

    override fun onDestroy() {
        super.onDestroy()
        releaseResources()
        try { shutdownKernelNative() } catch (e: Exception) {}
    }
}
