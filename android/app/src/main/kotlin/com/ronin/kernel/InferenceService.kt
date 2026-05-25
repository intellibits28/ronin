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
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import java.io.File

class InferenceService : Service() {
    private val TAG = "RoninKernel_Native"
    private val CHANNEL_ID = "ronin_inference_channel"
    private val NOTIFICATION_ID = 1001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var currentModelPath: String = ""
    
    // Phase 11.2: Context Management to prevent Error 13
    private var isConversationFresh = true
    
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun getFreeRamGBNative(): Float
    private external fun shutdownKernelNative()

    companion object {
        init {
            try { System.loadLibrary("ronin_kernel") } catch (e: Exception) {}
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
            .setContentTitle("Ronin Kernel: Neural Spine")
            .setContentText("Local Reasoning Active")
            .setSmallIcon(android.R.drawable.ic_menu_info_details)
            .build()
    }

    private val binder = object : IInferenceService.Stub() {
        override fun loadModel(modelPath: String): Boolean = tryHydrate(modelPath)
        
        override fun runReasoning(input: String): String = runBlocking {
            val fullResult = StringBuilder()
            executeInference(input).collect { fullResult.append(it) }
            fullResult.toString()
        }

        override fun streamReasoning(input: String, callback: IInferenceCallback) {
            serviceScope.launch {
                try {
                    executeInference(input).collect { token -> callback.onToken(token, false) }
                    callback.onToken("", true)
                } catch (e: Exception) {
                    callback.onError(e.message ?: "Unknown Error")
                }
            }
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
                Log.i(TAG, "Conversation Reset: Context Cleared.")
            } catch (e: Exception) {}
        }
    }

    override fun onBind(intent: Intent?): IBinder = binder

    private fun tryHydrate(path: String): Boolean {
        if (!File(path).exists()) return false
        releaseResources()

        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        // Hard check: Gemma 4 2B IT needs at least 1.2GB free to avoid Status 13 on SD778G+
        if (freeRam < 1.1f) return false

        return try {
            val config = EngineConfig(modelPath = path, maxNumTokens = 512) 
            val engine = Engine(config)
            engine.initialize()
            litertEngine = engine
            litertConversation = engine.createConversation()
            isConversationFresh = true
            currentModelPath = path
            true
        } catch (e: Throwable) { false }
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
        
        // Phase 11.2: Instruction Isolation
        // Instructions are ONLY sent if the conversation is fresh.
        val instructions = if (input.contains("[SYSTEM]")) input.substringAfter("[SYSTEM]").substringBefore("[USER]").trim() else ""
        val userPrompt = if (input.contains("[USER]")) input.substringAfter("[USER]").trim() else input
        
        try {
            if (isConversationFresh && instructions.isNotEmpty()) {
                Log.i(TAG, "Initializing session with system instructions...")
                // Note: High-level SDK might not have .system(), using first .user() as instructions
                val combinedInit = instructions + "\n\nUser: " + userPrompt
                conversation.sendMessageAsync(Message.user(combinedInit)).collect { partial ->
                    emit(extractToken(partial))
                }
                isConversationFresh = false
            } else {
                conversation.sendMessageAsync(Message.user(userPrompt)).collect { partial ->
                    emit(extractToken(partial))
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Inference failure: ${e.message}")
            throw e
        }
    }.flowOn(Dispatchers.IO)

    private fun extractToken(partial: Any): String {
        return try { 
            partial.javaClass.getMethod("getText").invoke(partial) as String 
        } catch(e: Exception) { partial.toString() }
    }

    override fun onDestroy() {
        super.onDestroy()
        releaseResources()
        try { shutdownKernelNative() } catch (e: Exception) {}
    }
}
