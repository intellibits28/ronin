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
 * Hardened v3.0 Inference Spine
 * Uses Direct JNI Callbacks for SHM-speed streaming within a single process.
 * Optimized for Snapdragon 778G+.
 */
class InferenceService : Service() {
    private val TAG = "RoninKernel_Worker"
    private val CHANNEL_ID = "ronin_hardened_spine"
    private val NOTIFICATION_ID = 3001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var currentModelPath: String = ""
    
    // Phase 11.2: Context State
    private var isConversationFresh = true
    
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    // --- Native JNI Interface ---
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun getFreeRamGBNative(): Float
    private external fun pushTokenToKernelNative(token: String, isFinal: Boolean)
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
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val manager = getSystemService(NotificationManager::class.java)
            manager?.createNotificationChannel(NotificationChannel(CHANNEL_ID, "Ronin Hardened Spine", NotificationManager.IMPORTANCE_LOW))
        }
    }

    private fun createNotification(): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Ronin Kernel: Hardened v3.0")
            .setContentText("Neural Spine Active (Single Process Mode)")
            .setSmallIcon(android.R.drawable.ic_menu_info_details)
            .build()
    }

    private val binder = object : IInferenceService.Stub() {
        override fun loadModel(modelPath: String): Boolean = tryHydrate(modelPath)
        
        /**
         * Hardened Synchronous Inference:
         * Returns full result to C++ kernel for persistent history, 
         * while fragments are streamed to UI via pushTokenToKernelNative.
         */
        override fun runReasoning(input: String): String = runBlocking(Dispatchers.IO) {
            val fullResult = StringBuilder()
            try {
                executeInference(input).collect { token ->
                    fullResult.append(token)
                    pushTokenToKernelNative(token, false)
                }
                pushTokenToKernelNative("", true)
                fullResult.toString()
            } catch (e: Exception) {
                val err = "Error: ${e.message}"
                pushTokenToKernelNative(err, true)
                err
            }
        }

        override fun streamReasoning(input: String, callback: IInferenceCallback) {
            // Forward to synchronous core in v3.0
            runReasoning(input)
        }

        override fun isHydrated(): Boolean = litertEngine != null
        override fun getActiveModelPath(): String = currentModelPath
        override fun notifyTrimMemory(level: Int) { if (level >= 80) resetConversation() }
        override fun setSafeMode(enabled: Boolean) {}
        override fun isLowPerformanceMode(): Boolean = false
        override fun resetConversation() {
            try {
                litertConversation?.close()
                litertConversation = litertEngine?.createConversation()
                isConversationFresh = true
            } catch (e: Exception) {}
        }
    }

    override fun onBind(intent: Intent?): IBinder = binder

    private fun tryHydrate(path: String): Boolean {
        if (!File(path).exists()) return false
        releaseResources()

        return try {
            // Hardened SD778G+ Settings:
            // 512 tokens limit for stable KV cache allocation.
            val config = EngineConfig(modelPath = path, maxNumTokens = 512)
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
        try {
            litertConversation?.close()
            litertConversation = null
            litertEngine?.close()
            litertEngine = null
        } catch (e: Exception) {}
    }

    private fun executeInference(input: String): Flow<String> = flow {
        val conversation = litertConversation ?: return@flow
        
        // Phase 11.2: Raw Prompt Passthrough (LiteRT-LM SDK wraps internally)
        val instructions = if (input.contains("[SYSTEM]")) input.substringAfter("[SYSTEM]").substringBefore("[USER]").trim() else ""
        val userPrompt = if (input.contains("[USER]")) input.substringAfter("[USER]").trim() else input
        
        val finalInput = if (isConversationFresh && instructions.isNotEmpty()) {
            isConversationFresh = false
            "$instructions\n\n$userPrompt"
        } else {
            userPrompt
        }

        if (finalInput.isEmpty()) return@flow

        // RAM Guard: Check before starting inference
        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        if (freeRam < 0.8f) {
            Log.w(TAG, "LOW RAM ALERT (%.2fGB). Resetting conversation.".format(freeRam))
            resetConversation() // Prune KV Cache
        }

        conversation.sendMessageAsync(Message.user(finalInput)).collect { partial ->
            val token = try { 
                partial.javaClass.getMethod("getText").invoke(partial) as String 
            } catch(e: Exception) { partial.toString() }
            emit(token)
        }
    }.flowOn(Dispatchers.IO)

    override fun onDestroy() {
        super.onDestroy()
        releaseResources()
        try { shutdownKernelNative() } catch (e: Exception) {}
    }
}
