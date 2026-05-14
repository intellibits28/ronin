package com.ronin.kernel

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.google.ai.edge.litertlm.Engine
import com.google.ai.edge.litertlm.EngineConfig
import com.google.ai.edge.litertlm.Conversation
import com.google.ai.edge.litertlm.Backend
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
import kotlinx.coroutines.flow.collect

class InferenceService : Service() {
    private val TAG = "RoninKernel_Native" // Aligned with native logs for filtered visibility
    private val CHANNEL_ID = "ronin_inference_channel"
    private val NOTIFICATION_ID = 1001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var currentModelPath: String = ""
    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    // JNI Bridge for Worker Process
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun pushTokenToSHMNative(fragment: String, isFinal: Boolean): Boolean
    private external fun getFreeRamGBNative(): Float

    companion object {
        init {
            try {
                System.loadLibrary("llm_inference_engine_jni")
                System.loadLibrary("ronin_kernel")
            } catch (e: UnsatisfiedLinkError) {
                Log.e("InferenceService", "Native linkage failed: ${e.message}")
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
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
            .setContentText("Local Reasoning Active (Snapdragon Optimized)")
            .setSmallIcon(android.R.drawable.ic_menu_info_details)
            .build()
    }

    private val binder = object : IInferenceService.Stub() {
        override fun loadModel(modelPath: String): Boolean {
            Log.i(TAG, "Loading model: $modelPath")
            return tryHydrate(modelPath)
        }

        override fun runReasoning(input: String): String {
            Log.d(TAG, "Running reasoning for: $input")
            return executeReasoning(input)
        }

        override fun isHydrated(): Boolean {
            return litertEngine != null
        }

        override fun getActiveModelPath(): String {
            return currentModelPath
        }

        override fun notifyTrimMemory(level: Int) {
            Log.i(TAG, "Memory trim notification: $level")
        }

        override fun setSafeMode(enabled: Boolean) {
            Log.w(TAG, "Safe Mode Toggle: $enabled (Thermal Guard)")
            isSafeModeActive = enabled
        }
    }

    private var isSafeModeActive = false

    override fun onBind(intent: Intent?): IBinder {
        Log.i(TAG, "Service bound")
        return binder
    }

    private fun tryHydrate(path: String): Boolean {
        Log.i(TAG, ">>> IPC Hydration Request for: $path")
        val modelFile = java.io.File(path)
        if (!modelFile.exists()) {
            Log.e(TAG, "FATAL: Model file NOT FOUND in :inference_core process at $path")
            return false
        }
        
        // Rule 5: RAM Guard before hydration
        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        Log.i(TAG, "Hydration Guard: Free RAM = ${freeRam}GB")
        val minRam = 1.5f
        if (freeRam < minRam) {
            Log.e(TAG, "ABORT: Insufficient RAM for Hydration. Available: ${freeRam}GB, Required: ${minRam}GB")
            return false
        }

        return try {
            // LiteRT 0.11.0: Using EngineConfig with Backend.GPU()
            val config = EngineConfig(
                modelPath = path,
                backend = Backend.GPU(),
                maxNumTokens = 1024
            )

            val engine = Engine(config)
            engine.initialize()
            litertEngine = engine

            // 0.11.0: Create conversation for session management
            litertConversation = engine.createConversation()

            currentModelPath = path
            Log.i(TAG, "SUCCESS: Gemma 4 Brain Hydrated (LiteRT 0.11.0) in :inference_core process.")
            true
        } catch (e: Throwable) {
            Log.e(TAG, "Hydration failed in service: ${e.message}")
            false
        }
    }

    private fun executeReasoning(input: String): String {
        val conversation = litertConversation ?: return "Error: Local reasoning spine not hydrated in service."
        
        Log.d(TAG, "Executing Reasoning [SafeMode: $isSafeModeActive].")
        
        return try {
            // LiteRT 0.11.0: Using sendMessageAsync which returns a Flow<String>
            // We collect tokens and push them to SHM from a dedicated scope
            serviceScope.launch(Dispatchers.IO) {
                try {
                    conversation.sendMessageAsync(input).collect { partialToken ->
                        pushTokenToSHMNative(partialToken, false)
                    }
                    // Signal completion
                    pushTokenToSHMNative("", true)
                    Log.i(TAG, "LiteRT Streaming Complete.")
                } catch (e: Exception) {
                    Log.e(TAG, "Streaming failure: ${e.message}")
                    pushTokenToSHMNative("Error: ${e.message}", true)
                }
            }
            
            "Reasoning Started [SHM Active]"
        } catch (e: Exception) {
            Log.e(TAG, "Inference crash in service: ${e.message}")
            "Error: Neural spine failure - ${e.message}"
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        litertConversation = null
        litertEngine = null
        Log.i(TAG, "Service destroyed")
    }
}
