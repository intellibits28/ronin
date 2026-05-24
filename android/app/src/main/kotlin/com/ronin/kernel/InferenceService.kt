package com.ronin.kernel

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.google.ai.edge.litertlm.*
import kotlinx.coroutines.flow.*
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.os.Build
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import java.io.File

/**
 * Phase 11.0: Unified LiteRT-LM Inference Service
 * Pure LiteRT-LM Implementation (MediaPipe-Free).
 * Optimized for Snapdragon 778G+ with explicit resource management.
 */
class InferenceService : Service() {
    private val TAG = "RoninKernel_Native"
    private val CHANNEL_ID = "ronin_inference_channel"
    private val NOTIFICATION_ID = 1001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var currentModelPath: String = ""

    // JNI Bridge for Worker Process (Core logic linkage)
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun getFreeRamGBNative(): Float
    private external fun shutdownKernelNative()

    companion object {
        init {
            try {
                System.loadLibrary("ronin_kernel")
            } catch (e: Exception) {
                Log.e("InferenceService", "Native linkage failed.")
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(NOTIFICATION_ID, createNotification(), android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } else {
            startForeground(NOTIFICATION_ID, createNotification())
        }

        try {
            val libDir = applicationInfo.nativeLibraryDir
            initializeKernelNative(filesDir.absolutePath, libDir, true)
        } catch (e: Throwable) {
            Log.e(TAG, "Worker JNI Initialization failed.")
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val serviceChannel = NotificationChannel(CHANNEL_ID, "Ronin Inference Engine", NotificationManager.IMPORTANCE_LOW)
            val manager = getSystemService(NotificationManager::class.java)
            manager?.createNotificationChannel(serviceChannel)
        }
    }

    private fun createNotification(): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Ronin Kernel: Neural Spine")
            .setContentText("Local Reasoning Active")
            .setSmallIcon(android.R.drawable.ic_menu_info_details)
            .build()
    }

    private val binder = object : IInferenceService.Stub() {
        override fun loadModel(modelPath: String): Boolean = tryHydrate(modelPath)
        override fun runReasoning(input: String): String = executeReasoningSync(input)
        override fun isHydrated(): Boolean = litertEngine != null
        override fun getActiveModelPath(): String = currentModelPath
        override fun notifyTrimMemory(level: Int) {
            if (level >= 80) resetConversation() 
        }
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
        val modelFile = File(path)
        if (!modelFile.exists()) return false
        
        // Critical: Purge MediaPipe leftovers and clear RAM before new hydration
        releaseResources()

        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        if (freeRam < 1.0f) {
            Log.w(TAG, "Insufficient RAM: $freeRam GB. Aborting hydration.")
            return false
        }

        return try {
            // Unified LiteRT-LM Engine Configuration
            val config = EngineConfig(
                modelPath = path, 
                maxNumTokens = 1024 
            )
            val engine = Engine(config)
            engine.initialize()
            
            litertEngine = engine
            litertConversation = engine.createConversation()
            currentModelPath = path
            
            Log.i(TAG, "SUCCESS: Gemma 4 hydrated via Unified LiteRT-LM SDK.")
            true
        } catch (e: Throwable) {
            Log.e(TAG, "LiteRT-LM Init Failed: ${e.message}")
            false
        }
    }

    private fun releaseResources() {
        try {
            litertConversation?.close()
            litertConversation = null
            litertEngine?.close()
            litertEngine = null
            Log.i(TAG, "Cleaned engine resources.")
        } catch (e: Exception) {}
    }

    private fun executeReasoningSync(input: String): String = runBlocking(Dispatchers.IO) {
        val conversation = litertConversation ?: return@runBlocking "Error: Spine not hydrated."

        // Clean input for LiteRT-LM (Remove all manual P1/P2 tags)
        val cleanInput = input
            .replace("[SYSTEM]", "")
            .replace("[USER]", "")
            .trim()
        
        try {
            val userMsg = Message.user(cleanInput)
            val fullResult = StringBuilder()
            
            conversation.sendMessageAsync(userMsg).collect { partial ->
                // Flexible token extraction to handle SDK variations
                val token = try { 
                    partial.javaClass.getMethod("getText").invoke(partial) as String 
                } catch(e: Exception) { partial.toString() }
                fullResult.append(token)
            }
            
            val finalResponse = fullResult.toString()
            if (finalResponse.isEmpty()) "Error: Empty response from model." else finalResponse
            
        } catch (e: Exception) {
            Log.e(TAG, "Inference Failed: ${e.message}")
            "Error: ${e.message}"
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        releaseResources()
        try { shutdownKernelNative() } catch (e: Exception) {}
    }
}
