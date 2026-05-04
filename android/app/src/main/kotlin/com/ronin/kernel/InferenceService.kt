package com.ronin.kernel

import android.app.Service
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
        return try {
            val builder = LlmInference.LlmInferenceOptions.builder()
                .setModelPath(path)
                .setMaxTokens(512)
            
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
        
        val formattedPrompt = if (currentModelPath.endsWith(".litertlm")) {
            "<|turn>user\n$input<turn|>\n<|turn>model\n"
        } else {
            "<start_of_turn>user\n$input<end_of_turn>\n<start_of_turn>model\n"
        }
        
        return try {
            val startTime = System.currentTimeMillis()
            val response = inference.generateResponse(formattedPrompt)
            val duration = System.currentTimeMillis() - startTime
            
            if (response.isEmpty()) {
                Log.w(TAG, "!!! Empty response received in service.")
                return "Error: Empty response"
            }

            Log.i(TAG, "Neural Response SUCCESS in ${duration}ms")
            response
        } catch (e: Exception) {
            Log.e(TAG, "Inference error in service: ${e.message}")
            "Error: Neural spine failure - ${e.message}"
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        llmInference?.close()
        Log.i(TAG, "Service destroyed")
    }
}
