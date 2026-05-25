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
 * Uses Direct JNI Callbacks for SHM-speed streaming without transport overhead.
 */
class InferenceService : Service() {
    private val TAG = "RoninKernel_Worker"
    private val CHANNEL_ID = "ronin_hardened_spine"
    private val NOTIFICATION_ID = 3001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var currentModelPath: String = ""
    
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    // --- Native JNI Interface (Hardened v3.0) ---
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun getFreeRamGBNative(): Float
    private external fun pushTokenToKernelNative(token: String, isFinal: Boolean)
    private external fun shutdownKernelNative()

    companion object {
        init {
            try { System.loadLibrary("ronin_kernel") } catch (e: Exception) {
                Log.e("RoninKernel", "Failed to load native library in worker.")
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, createNotification())
        
        // Connect Worker to Native Kernel
        try {
            initializeKernelNative(filesDir.absolutePath, applicationInfo.nativeLibraryDir, true)
        } catch (e: Throwable) {}
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
            .setContentText("Neural Spine Active (Snapdragon Optimization)")
            .setSmallIcon(android.R.drawable.ic_menu_info_details)
            .build()
    }

    private val binder = object : IInferenceService.Stub() {
        override fun loadModel(modelPath: String): Boolean = tryHydrate(modelPath)
        
        /**
         * Hardened Sync Trigger: 
         * Results are streamed via pushTokenToKernelNative for zero-lag UI update.
         */
        override fun runReasoning(input: String): String {
            serviceScope.launch {
                executeInference(input).collect { token ->
                    pushTokenToKernelNative(token, false)
                }
                pushTokenToKernelNative("", true)
            }
            return "STREAMING_ACTIVE"
        }

        override fun streamReasoning(input: String, callback: IInferenceCallback) {
            // Deprecated in v3.0 in favor of Direct JNI Bridge, but kept for legacy compat.
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
            } catch (e: Exception) {}
        }
    }

    override fun onBind(intent: Intent?): IBinder = binder

    private fun tryHydrate(path: String): Boolean {
        if (!File(path).exists()) return false
        releaseResources()

        return try {
            // Hardened SD778G+ Settings:
            // 1. Lower tokens to 512 for prefill stability.
            // 2. Explicit cache to prevent storage bloat.
            val config = EngineConfig(
                modelPath = path, 
                maxNumTokens = 512,
                topK = 40,
                temperature = 0.7f
            )
            val engine = Engine(config)
            engine.initialize()
            litertEngine = engine
            litertConversation = engine.createConversation()
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
        
        // Clean prompt for LiteRT-LM (Raw Injection)
        val cleanInput = input
            .replace("[SYSTEM]", "")
            .replace("[USER]", "")
            .trim()
        
        if (cleanInput.isEmpty()) return@flow

        try {
            conversation.sendMessageAsync(Message.user(cleanInput)).collect { partial ->
                val token = try { 
                    partial.javaClass.getMethod("getText").invoke(partial) as String 
                } catch(e: Exception) { partial.toString() }
                emit(token)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Inference Fault: ${e.message}")
            emit("Error: ${e.message}")
        }
    }.flowOn(Dispatchers.IO)

    override fun onDestroy() {
        super.onDestroy()
        releaseResources()
        try { shutdownKernelNative() } catch (e: Exception) {}
    }
}
