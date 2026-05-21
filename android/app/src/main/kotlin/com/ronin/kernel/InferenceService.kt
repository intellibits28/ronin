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

class InferenceService : Service() {
    private val TAG = "RoninKernel_Native"
    private val CHANNEL_ID = "ronin_inference_channel"
    private val NOTIFICATION_ID = 1001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var legacyInference: LlmInference? = null
    private var currentInferenceJob: Job? = null
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
    private external fun shutdownKernelNative()

    companion object {
        init {
            try {
                System.loadLibrary("ronin_kernel")
            } catch (e: UnsatisfiedLinkError) {
                Log.e("InferenceService", "Native linkage failed: ${e.message}")
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

        override fun resetConversation() {
            Log.i(TAG, "Manual Neural Reset: Clearing KV Cache Context.")
            currentInferenceJob?.cancel()
            try {
                litertEngine?.let { engine ->
                    litertConversation = engine.createConversation()
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to reset conversation: ${e.message}")
            }
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
        
        val fileSize = modelFile.length()
        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        
        val minRam = when {
            fileSize < 800 * 1024 * 1024 -> 1.0f 
            fileSize < 1500 * 1024 * 1024 -> 1.2f 
            else -> 1.5f 
        }
        
        Log.i(TAG, "Hydration Guard: FileSize=${fileSize/(1024*1024)}MB, FreeRAM=${freeRam}GB, Required=${minRam}GB")
        
        if (freeRam < minRam) {
            Log.e(TAG, "ABORT: Insufficient RAM. Available: ${freeRam}GB, Required: ${minRam}GB")
            return false
        }

        val cacheDirFile = java.io.File(filesDir, "models/cache")
        if (!cacheDirFile.exists()) cacheDirFile.mkdirs()
        
        clearOrphanedArtifacts()

        val modelStatus = getModelStatus(path)
        isLowPerformanceMode = false

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
        val optionsBuilder = LlmInference.LlmInferenceOptions.builder()
            .setModelPath(path)
            .setMaxTokens(1024)
            
        // Phase 8.6: Removed setResultListener to resolve CI linkage errors.
        // Legacy models will use synchronous generateResponse() in executeReasoning().
            
        if (status != STATUS_STABILITY_ISSUE) {
            try {
                optionsBuilder.setPreferredBackend(LlmInference.Backend.GPU)
            } catch (e: Exception) {
                Log.w(TAG, "Legacy GPU failed: ${e.message}")
            }
        }

        legacyInference = LlmInference.createFromOptions(this, optionsBuilder.build())
        currentModelPath = path
        return true
    }

    private fun hydrateLiteRT(path: String, status: String, cacheDir: java.io.File): Boolean {
        val backends = if (status == STATUS_STABILITY_ISSUE) {
            listOf(Backend.CPU())
        } else {
            listOf(Backend.GPU(), Backend.CPU())
        }

        var engine: Engine? = null
        var lastError: String? = null

        for (backend in backends) {
            try {
                Log.i(TAG, "Attempting hydration with backend: $backend")
                val config = EngineConfig(
                    modelPath = path,
                    backend = backend,
                    maxNumTokens = 1024
                )
                
                engine = Engine(config)
                engine.initialize()
                
                if (backend is Backend.CPU) isLowPerformanceMode = true
                break
            } catch (e: Exception) {
                lastError = e.message
                Log.w(TAG, "Backend $backend failed: $lastError")
            }
        }

        if (engine == null) return false
        
        litertEngine = engine
        litertConversation = engine.createConversation()
        override fun resetConversation() {
            Log.i(TAG, "Manual Neural Reset: Clearing KV Cache Context.")
            currentInferenceJob?.cancel()
            try {
        ...
        private fun executeReasoning(input: String): String {
        val litert = litertConversation
        val legacy = legacyInference

        if (litert == null && legacy == null) {
            return "Error: Local reasoning spine not hydrated in service."
        }

        // Phase 9.1: Detect Thinking Mode Trigger
        val isThinkingRequest = input.startsWith("[THINK]")
        val actualInput = if (isThinkingRequest) input.removePrefix("[THINK]") else input

        Log.d(TAG, "Executing Reasoning [Thinking: $isThinkingRequest].")

        return try {
            if (litert != null) {
                currentInferenceJob?.cancel()
                currentInferenceJob = serviceScope.launch(Dispatchers.IO) {
                    try {
                        // For Gemma 4, we need to construct the prompt manually for CoT injection
                        val isGemma4 = currentModelPath.contains("gemma-4")

                        
                        if (isThinkingRequest && isGemma4) {
                            // Phase 9.2: Refined CoT Prompting (User-requested logic)
                            val cotInstructions = "You are Ronin Kernel Core. Before giving the final answer, you MUST think step-by-step. Break down the entities, relations, and logic inside <thinking> tags, then provide the precise response."
                            val cotInput = "$cotInstructions\n\n$actualInput"
                            val userMsg = Message.user(cotInput)
                            litert.sendMessageAsync(userMsg).collect { partialMessage ->
                                val token = try { partialMessage.javaClass.getMethod("getText").invoke(partialMessage) as String } catch(e: Exception) { partialMessage.toString() }
                                pushTokenToSHMNative(token, false)
                            }
                        } else {
                            val userMsg = Message.user(actualInput)
                            litert.sendMessageAsync(userMsg).collect { partialMessage ->
                                val token = try { partialMessage.javaClass.getMethod("getText").invoke(partialMessage) as String } catch(e: Exception) { partialMessage.toString() }
                                pushTokenToSHMNative(token, false)
                            }
                        }
                        pushTokenToSHMNative("", true)
                        Log.i(TAG, "Inference Stream Complete.")
                    } catch (e: Exception) {
                        Log.e(TAG, "Inference failure: ${e.message}")
                        pushTokenToSHMNative("Error: ${e.message}", true)
                    }
                }
            } else if (legacy != null) {
                try {
                    val response = legacy.generateResponse(actualInput)
                    if (!response.isNullOrEmpty()) {
                        pushTokenToSHMNative(response, true)
                        Log.i(TAG, "Legacy reasoning complete and pushed to SHM.")
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Legacy reasoning failure: ${e.message}")
                    "Error: Legacy engine failure - ${e.message}"
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
        try {
            shutdownKernelNative()
        } catch (e: Exception) {}
        Log.i(TAG, "Service destroyed")
    }
}
