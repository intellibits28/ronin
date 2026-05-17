package com.ronin.kernel

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.google.ai.edge.litertlm.*
import com.google.mediapipe.tasks.genai.llminference.LlmInference
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

class InferenceService : Service() {
    private val TAG = "RoninKernel_Native" // Aligned with native logs for filtered visibility
    private val CHANNEL_ID = "ronin_inference_channel"
    private val NOTIFICATION_ID = 1001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var legacyInference: LlmInference? = null
    private var currentModelPath: String = ""
    private var isLowPerformanceMode = false
    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val PREFS_NAME = "ronin_model_registry"
    private val STATUS_STABILITY_ISSUE = "STABILITY_ISSUE"
    private val STATUS_OK = "OK"

    // JNI Bridge for Worker Process
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun pushTokenToSHMNative(fragment: String, isFinal: Boolean): Boolean
    private external fun getFreeRamGBNative(): Float

    companion object {
        init {
            try {
                // Proactively load LiteRT runtime to ensure symbols are available for ronin_kernel
                System.loadLibrary("litert")
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
            return litertEngine != null || legacyInference != null
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

        override fun isLowPerformanceMode(): Boolean {
            return this@InferenceService.isLowPerformanceMode
        }
    }

    private var isSafeModeActive = false

    override fun onBind(intent: Intent?): IBinder {
        Log.i(TAG, "Service bound")
        return binder
    }

    private fun getModelStatus(path: String): String {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getString(path, STATUS_OK) ?: STATUS_OK
    }

    private fun setModelStatus(path: String, status: String) {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().putString(path, status).apply()
    }

    private fun clearOrphanedArtifacts() {
        try {
            val cacheDir = java.io.File(filesDir, "models/cache")
            if (cacheDir.exists()) {
                val files = cacheDir.listFiles()
                files?.forEach { 
                    if (it.name.contains("_")) {
                        Log.i(TAG, "Cleaning orphaned artifact: ${it.name}")
                        it.delete()
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Artifact cleanup failed: ${e.message}")
        }
    }

    private fun tryHydrate(path: String): Boolean {
        Log.i(TAG, ">>> IPC Hydration Request for: $path")
        val modelFile = java.io.File(path)
        if (!modelFile.exists()) {
            Log.e(TAG, "FATAL: Model file NOT FOUND in :inference_core process at $path")
            return false
        }
        
        // Phase 9.0: Adaptive RAM Guard (Rule v2.1)
        val fileSize = modelFile.length()
        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        
        val minRam = when {
            fileSize < 800 * 1024 * 1024 -> 1.0f // <800MB (1B models)
            fileSize < 1500 * 1024 * 1024 -> 1.2f // <1.5GB (2B models)
            else -> 1.5f // Gemma 4
        }
        
        Log.i(TAG, "Hydration Guard: FileSize=${fileSize/(1024*1024)}MB, FreeRAM=${freeRam}GB, Required=${minRam}GB")
        
        if (freeRam < minRam) {
            Log.e(TAG, "ABORT: Insufficient RAM. Available: ${freeRam}GB, Required: ${minRam}GB")
            return false
        }

        // Isolation: Create models/cache directory
        val cacheDirFile = java.io.File(filesDir, "models/cache")
        if (!cacheDirFile.exists()) cacheDirFile.mkdirs()
        
        clearOrphanedArtifacts()

        val modelStatus = getModelStatus(path)
        isLowPerformanceMode = false

        // Determine Backend by Extension
        val isLegacy = path.endsWith(".bin")

        return try {
            if (isLegacy) {
                hydrateLegacy(path, modelStatus)
            } else {
                hydrateLiteRT(path, modelStatus, cacheDirFile)
            }
        } catch (e: Throwable) {
            Log.e(TAG, "Hydration failed in service: ${e.message}")
            setModelStatus(path, STATUS_STABILITY_ISSUE)
            false
        }
    }

    private fun hydrateLegacy(path: String, status: String): Boolean {
        Log.i(TAG, "Backing up to Legacy MediaPipe Engine for .bin model.")
        val options = LlmInference.LlmInferenceOptions.builder()
            .setModelPath(path)
            .setMaxTokens(1024)
            
        if (status != STATUS_STABILITY_ISSUE) {
            try {
                // MediaPipe 0.10.35 uses setPreferredBackend for hardware acceleration
                options.setPreferredBackend(LlmInference.Backend.GPU)
            } catch (e: Exception) {
                Log.w(TAG, "Legacy GPU failed: ${e.message}")
            }
        }

        legacyInference = LlmInference.createFromOptions(this, options.build())
        currentModelPath = path
        return true
    }

    private fun hydrateLiteRT(path: String, status: String, cacheDir: java.io.File): Boolean {
        val backends = if (status == STATUS_STABILITY_ISSUE) {
            listOf(Backend.CPU())
        } else {
            listOf(Backend.NPU(nativeLibraryDir = applicationInfo.nativeLibraryDir), Backend.GPU(), Backend.CPU())
        }

        var engine: Engine? = null
        var lastError: String? = null

        for (backend in backends) {
            try {
                Log.i(TAG, "Attempting hydration with backend: ${backend.javaClass.simpleName}")
                val config = EngineConfig(
                    modelPath = path,
                    backend = backend,
                    maxNumTokens = 1024,
                    cacheDir = cacheDir.absolutePath
                )
                engine = Engine(config)
                engine.initialize()
                
                if (backend is Backend.CPU) isLowPerformanceMode = true
                break
            } catch (e: Exception) {
                lastError = e.message
                Log.w(TAG, "Backend ${backend.javaClass.simpleName} failed: $lastError")
            }
        }

        if (engine == null) return false
        
        litertEngine = engine
        litertConversation = engine.createConversation()
        currentModelPath = path
        return true
    }

    private fun executeReasoning(input: String): String {
        val litert = litertConversation
        val legacy = legacyInference

        if (litert == null && legacy == null) {
            return "Error: Local reasoning spine not hydrated in service."
        }

        Log.d(TAG, "Executing Reasoning [SafeMode: $isSafeModeActive].")

        return try {
            if (litert != null) {
                // LiteRT 0.11.0 Flow
                serviceScope.launch(Dispatchers.IO) {
                    try {
                        val userMessage = Message.user(input)
                        litert.sendMessageAsync(userMessage).collect { partialMessage ->
                            val token = partialMessage.toString()
                            pushTokenToSHMNative(token, false)
                        }
                        pushTokenToSHMNative("", true)
                        Log.i(TAG, "LiteRT Streaming Complete.")
                    } catch (e: Exception) {
                        Log.e(TAG, "LiteRT Streaming failure: ${e.message}")
                        pushTokenToSHMNative("Error: ${e.message}", true)
                    }
                }
            } else if (legacy != null) {
                // Legacy MediaPipe 0.10.x Flow
                try {
                    legacy.generateResponseAsync(input) { result, done ->
                        pushTokenToSHMNative(result, done)
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Legacy Streaming failure: ${e.message}")
                    return "Error: Legacy engine failure - ${e.message}"
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
        legacyInference?.close()
        Log.i(TAG, "Service destroyed")
    }
}
