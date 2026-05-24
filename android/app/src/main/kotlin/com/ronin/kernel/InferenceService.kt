package com.ronin.kernel

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.google.ai.edge.litertlm.*
import com.google.mediapipe.tasks.genai.llminference.*
import kotlinx.coroutines.flow.*
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.os.Build
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.Job
import kotlinx.coroutines.runBlocking

class InferenceService : Service() {
    private val TAG = "RoninKernel_Native"
    private val CHANNEL_ID = "ronin_inference_channel"
    private val NOTIFICATION_ID = 1001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var legacyInference: LlmInference? = null
    private var currentModelPath: String = ""
    private var isLowPerformanceMode = false

    private val PREFS_NAME = "ronin_model_registry"
    private val STATUS_STABILITY_ISSUE = "STABILITY_ISSUE"
    private val STATUS_OK = "OK"

    // JNI Bridge for Worker Process (SHM methods removed)
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
            Log.e(TAG, "Worker JNI Initialization failed: ${e.message}")
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val serviceChannel = NotificationChannel(
                CHANNEL_ID,
                "Ronin Inference Engine",
                NotificationManager.IMPORTANCE_LOW
            )
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
        override fun loadModel(modelPath: String): Boolean {
            return tryHydrate(modelPath)
        }

        override fun runReasoning(input: String): String {
            // Synchronous execution for Single Model Architecture
            return executeReasoningSync(input)
        }

        override fun isHydrated(): Boolean {
            return litertEngine != null || legacyInference != null
        }

        override fun getActiveModelPath(): String {
            return currentModelPath
        }

        override fun notifyTrimMemory(level: Int) {
            Log.i(TAG, "Memory trim: $level")
        }

        override fun setSafeMode(enabled: Boolean) {
            isSafeModeActive = enabled
        }

        override fun isLowPerformanceMode(): Boolean {
            return this@InferenceService.isLowPerformanceMode
        }

        override fun resetConversation() {
            try {
                litertConversation?.close()
                litertConversation = litertEngine?.createConversation()
            } catch (e: Exception) {}
        }
    }

    private var isSafeModeActive = false

    override fun onBind(intent: Intent?): IBinder {
        return binder
    }

    private fun tryHydrate(path: String): Boolean {
        val modelFile = java.io.File(path)
        if (!modelFile.exists()) return false
        
        val fileSize = modelFile.length()
        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        val minRam = if (fileSize < 800 * 1024 * 1024) 1.0f else 1.5f 
        
        if (freeRam < minRam) return false

        val cacheDirFile = java.io.File(filesDir, "models/cache")
        if (!cacheDirFile.exists()) cacheDirFile.mkdirs()
        
        val isLegacy = path.endsWith(".bin")
        return try {
            if (isLegacy) {
                hydrateLegacy(path)
            } else {
                hydrateLiteRT(path)
            }
        } catch (e: Throwable) {
            false
        }
    }

    private fun hydrateLegacy(path: String): Boolean {
        val options = LlmInference.LlmInferenceOptions.builder()
            .setModelPath(path)
            .setMaxTokens(1024)
            .build()
        legacyInference = LlmInference.createFromOptions(this, options)
        currentModelPath = path
        return true
    }

    private fun hydrateLiteRT(path: String): Boolean {
        val config = EngineConfig(modelPath = path, maxNumTokens = 1024)
        val engine = Engine(config)
        engine.initialize()
        litertEngine = engine
        litertConversation = engine.createConversation()
        currentModelPath = path
        return true
    }

    private fun executeReasoningSync(input: String): String = runBlocking(Dispatchers.IO) {
        val litert = litertConversation
        val legacy = legacyInference

        if (litert == null && legacy == null) return@runBlocking "Error: Spine not hydrated."

        val actualInput = input.removePrefix("[THINK]")
        
        try {
            if (litert != null) {
                val userMsg = Message.user(actualInput)
                val fullResult = StringBuilder()
                litert.sendMessageAsync(userMsg).collect { partial ->
                    val token = try { 
                        partial.javaClass.getMethod("getText").invoke(partial) as String 
                    } catch(e: Exception) { partial.toString() }
                    fullResult.append(token)
                }
                fullResult.toString()
            } else {
                legacy?.generateResponse(actualInput) ?: "Error: Legacy failure."
            }
        } catch (e: Exception) {
            "Error: ${e.message}"
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        litertConversation = null
        litertEngine = null
        legacyInference?.close()
        try { shutdownKernelNative() } catch (e: Exception) {}
    }
}
