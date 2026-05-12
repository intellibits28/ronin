package com.ronin.kernel

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.google.mediapipe.tasks.genai.llminference.LlmInference
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
    private val TAG = "RoninInferenceService"
    private val CHANNEL_ID = "ronin_inference_channel"
    private val NOTIFICATION_ID = 1001

    private var llmInference: LlmInference? = null
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
            return llmInference != null
        }

        override fun getActiveModelPath(): String {
            return currentModelPath
        }

        override fun notifyTrimMemory(level: Int) {
            Log.i(TAG, "Memory trim notification: $level")
            if (level >= android.content.ComponentCallbacks2.TRIM_MEMORY_MODERATE) {
                // In a more complex scenario, we might release the engine here
                // For now, we rely on the system killing the process if needed
            }
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
        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f } // Fallback to safe guess
        val minRam = 1.5f // 1.5GB threshold for Gemma 4
        if (freeRam < minRam) {
            Log.e(TAG, "ABORT: Insufficient RAM for Hydration. Available: ${freeRam}GB, Required: ${minRam}GB")
            return false
        }

        if (!modelFile.canRead()) {
            Log.e(TAG, "FATAL: Model file NOT READABLE in :inference_core process.")
            return false
        }

        return try {
            val builder = LlmInference.LlmInferenceOptions.builder()
                .setModelPath(path)
                .setMaxTokens(1024)
            
            // Phase 6.7: Hardware Acceleration Audit
            // Enable GPU delegate for Snapdragon 778G+ optimization
            try {
                // MediaPipe GenAI uses the following path for GPU delegate
                builder.setDelegate(LlmInference.LlmInferenceOptions.Delegate.GPU)
                Log.i(TAG, "Hardware Acceleration: GPU Delegate ENABLED.")
            } catch (e: Exception) {
                Log.w(TAG, "GPU Delegate setup failed: ${e.message}. Falling back to CPU.")
            }

            llmInference = LlmInference.createFromOptions(this, builder.build())
            currentModelPath = path
            Log.i(TAG, "SUCCESS: Gemma 4 Brain Hydrated in :inference_core process.")
            true
        } catch (e: Throwable) {
            Log.e(TAG, "Hydration failed in service: ${e.message}")
            false
        }
    }

    private fun executeReasoning(input: String): String {
        val inference = llmInference ?: return "Error: Local reasoning spine not hydrated in service."
        
        val isLiteRTLM = currentModelPath.endsWith(".litertlm")
        val isGemma = currentModelPath.lowercase().contains("gemma")
        
        val formattedPrompt = when {
            isLiteRTLM -> "<|turn|>user\n$input<|turn|>model\n"
            isGemma -> "<start_of_turn>user\n$input<end_of_turn>\n<start_of_turn>model\n"
            else -> input // Fallback for raw models
        }
        
        Log.d(TAG, "Executing Reasoning [SafeMode: $isSafeModeActive].")
        
        return try {
            // Rule 8: If safe mode is active, we might want to log it or use lower priority
            // But generateResponse is sync and doesn't take priority. 
            // We just log it for now.
            if (isSafeModeActive) {
                Log.w(TAG, "Neural Reasoning under Thermal Stress (Safe Mode Active).")
            }

            val response = inference.generateResponse(formattedPrompt)
            if (!response.isNullOrEmpty()) {
                val cleaned = response
                    .replace("<|turn|>", "")
                    .replace("<turn|>", "")
                    .replace("<|turn>", "")
                    .replace("turn|user", "")
                    .replace("turn|model", "")
                    .replace("<start_of_turn>", "")
                    .replace("<end_of_turn>", "")
                    .trim()
                
                // Final safety check before native call
                if (llmInference != null) {
                    // Phase 8.2: UTF-8 Safe Chunking (Burmese Glyphs Protection)
                    val bytes = cleaned.toByteArray(Charsets.UTF_8)
                    var offset = 0
                    while (offset < bytes.size) {
                        val remaining = bytes.size - offset
                        val maxChunk = 255
                        var actualChunkSize = Math.min(maxChunk, remaining)
                        
                        // If we're not at the very end, check the next byte to see if we're splitting a character
                        if (offset + actualChunkSize < bytes.size) {
                            // UTF-8 continuation byte check: (byte & 0xC0) == 0x80
                            // We back up actualChunkSize until we hit a non-continuation byte
                            while (actualChunkSize > 0 && (bytes[offset + actualChunkSize].toInt() and 0xC0) == 0x80) {
                                actualChunkSize--
                            }
                        }
                        
                        // Safety fallback: if we backed up to 0, just take the original chunk size
                        if (actualChunkSize == 0) actualChunkSize = Math.min(maxChunk, remaining)

                        val chunk = String(bytes, offset, actualChunkSize, Charsets.UTF_8)
                        val isFinalChunk = (offset + actualChunkSize >= bytes.size)
                        
                        pushTokenToSHMNative(chunk, isFinalChunk)
                        offset += actualChunkSize
                    }
                    Log.i(TAG, "Neural Response pushed to SHM highway safely.")
                }
                "Reasoning Started [SHM Active]"
            } else {
                "Error: Empty response from engine."
            }
        } catch (e: Exception) {
            Log.e(TAG, "Inference crash in service: ${e.message}")
            "Error: Neural spine failure - ${e.message}"
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        llmInference?.close()
        Log.i(TAG, "Service destroyed")
    }
}
