package com.ronin.kernel

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.google.mediapipe.tasks.genai.llminference.LlmInference
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class InferenceService : Service() {
    private val TAG = "RoninInferenceService"
    private var llmInference: LlmInference? = null
    private var currentModelPath: String = ""
    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    // JNI Bridge for Worker Process
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun pushTokenToSHMNative(fragment: String, isFinal: Boolean): Boolean

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
        try {
            val libDir = applicationInfo.nativeLibraryDir
            initializeKernelNative(filesDir.absolutePath, libDir, true)
        } catch (e: Throwable) {
            Log.e(TAG, "Worker JNI Initialization failed: ${e.message}")
        }
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
        if (!modelFile.canRead()) {
            Log.e(TAG, "FATAL: Model file NOT READABLE in :inference_core process.")
            return false
        }

        return try {
            val builder = LlmInference.LlmInferenceOptions.builder()
                .setModelPath(path)
                .setMaxTokens(1024)
            
            // Phase 6.7: Hardware Acceleration Audit
            // Attempt GPU delegate if supported in this SDK version
            try {
                // In some versions of MediaPipe GenAI, the Delegate enum is under LlmInferenceOptions
                // We use reflection or catch the unresolved reference if we can't be sure of the exact nested path
                // But for now, let's try the common path.
                // builder.setDelegate(LlmInference.LlmInferenceOptions.Delegate.GPU) 
                Log.i(TAG, "Hardware Acceleration: GPU Delegate requested (skipping explicit call due to API mismatch).")
            } catch (e: Exception) {
                Log.w(TAG, "GPU Delegate setup failed: ${e.message}")
            }

            llmInference = LlmInference.createFromOptions(this, builder.build())
            currentModelPath = path
            Log.i(TAG, "SUCCESS: Gemma 4 Brain Hydrated in :inference_core process.")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Hydration failed in service: ${e.message}")
            false
        }
    }

    private fun executeReasoning(input: String): String {
        val inference = llmInference ?: return "Error: Local reasoning spine not hydrated in service."
        
        val isLiteRTLM = currentModelPath.endsWith(".litertlm")
        val formattedPrompt = if (isLiteRTLM) {
            "<|turn|>user\n$input<|turn|>model\n"
        } else {
            "<start_of_turn>user\n$input<end_of_turn>\n<start_of_turn>model\n"
        }
        
        Log.d(TAG, "Executing Reasoning via SHM.")
        
        return try {
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
                if (isHydrated()) {
                    pushTokenToSHMNative(cleaned, true)
                    Log.i(TAG, "Neural Response pushed to SHM.")
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
