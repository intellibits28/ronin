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
        
        // Memory Audit before generation
        val am = getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager
        val mi = android.app.ActivityManager.MemoryInfo()
        am.getMemoryInfo(mi)
        Log.i(TAG, "Memory state before inference: Avail=${mi.availMem/1024/1024}MB, LowMem=${mi.lowMemory}")

        // Phase 6.6: Refined Template Logic for Gemma 4
        val isLiteRTLM = currentModelPath.endsWith(".litertlm")
        val formattedPrompt = if (isLiteRTLM) {
            "<|turn|>user\n$input<|turn|>model\n"
        } else {
            "<start_of_turn>user\n$input<end_of_turn>\n<start_of_turn>model\n"
        }
        
        Log.d(TAG, "Executing Reasoning [Type: ${if(isLiteRTLM) "LiteRT-LM" else "Legacy"}]. Length: ${formattedPrompt.length} chars.")
        
        return try {
            val startTime = System.currentTimeMillis()
            val response = inference.generateResponse(formattedPrompt)
            val duration = System.currentTimeMillis() - startTime
            
            if (response.isNullOrEmpty()) {
                Log.w(TAG, "!!! CRITICAL: Gemma 4 returned NULL/EMPTY response after ${duration}ms.")
                return "Error: Empty response from neural spine. Check logcat for details."
            }

            // Phase 4.5.8: Post-processing to strip internal artifacts (turn|user, etc.)
            val cleanedResponse = response
                .replace("<|turn|>", "")
                .replace("<turn|>", "")
                .replace("<|turn>", "")
                .replace("turn|user", "")
                .replace("turn|model", "")
                .replace("<start_of_turn>", "")
                .replace("<end_of_turn>", "")
                .trim()

            Log.i(TAG, "Neural Response SUCCESS in ${duration}ms. Tokens generated: ~${cleanedResponse.length / 4}")
            cleanedResponse
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
