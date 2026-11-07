package com.ronin.kernel

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.util.Log
import com.google.ai.edge.litertlm.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.annotation.Keep
import java.io.File
import org.json.JSONObject

/**
 * Hardened v4.0 Inference Spine
 * Optimized for Myanmar text (1024 tokens) and robust multi-process JNI stability.
 */
@Keep
class InferenceService : Service() {
    private val TAG = "RoninKernel_Worker"
    private val CHANNEL_ID = "ronin_hardened_spine"
    private val NOTIFICATION_ID = 3001

    private var litertEngine: Engine? = null
    private var litertConversation: Conversation? = null
    private var currentModelPath: String = ""
    
    private var currentTemp = 0.7f
    private var currentTopK = 40
    private var currentTopP = 0.9f
    private var currentMaxTokens = 2048
    
    private var isConversationFresh = true
    private var lastSummary: String? = null
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private val inferenceMutex = Mutex()
    @Volatile
    private var cachedGetTextMethod: java.lang.reflect.Method? = null
    @Volatile
    private var getTextLookupDone: Boolean = false

    // --- Native JNI Interface (Essential Only) ---
    private external fun initializeKernelNative(filesDir: String, libDir: String, isWorker: Boolean)
    private external fun getFreeRamGBNative(): Float
    private external fun shutdownKernelNative()

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, createNotification())
        try {
            initializeKernelNative(filesDir.absolutePath, applicationInfo.nativeLibraryDir, true)
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
            .setContentTitle("Ronin Kernel: Hardened v3.5")
            .setContentText("Neural Spine Active (v3.5 Build)")
            .setSmallIcon(android.R.drawable.ic_menu_info_details)
            .build()
    }

    private fun resetConversationLocked() {
        synchronized(this) {
            try {
                litertConversation?.close()
                litertConversation = createConversationLocked()
                isConversationFresh = true
                Log.i(TAG, "Conversation Reset: KV Cache Pruned.")
            } catch (e: Exception) { Log.e(TAG, "Reset failed: ${e.message}") }
        }
    }

    private fun createConversationLocked(): Conversation? {
        val sampler = SamplerConfig(
            topK = currentTopK.coerceAtLeast(1),
            topP = currentTopP.coerceIn(0.0f, 1.0f).toDouble(),
            temperature = currentTemp.coerceAtLeast(0.0f).toDouble(),
            seed = 42
        )
        return litertEngine?.createConversation(ConversationConfig(samplerConfig = sampler))
    }

    private fun resetConversationInternal() {
        runBlocking(Dispatchers.IO) {
            inferenceMutex.withLock {
                resetConversationLocked()
            }
        }
    }

    private val binder = object : IInferenceService.Stub() {
        override fun loadModel(modelPath: String): String = tryHydrate(modelPath)
        
        override fun runReasoning(input: String): String = runBlocking(Dispatchers.IO) {
            val fullResult = StringBuilder()
            try { executeInference(input).collect { token -> fullResult.append(token) } ; fullResult.toString() } 
            catch (e: Exception) { "Error: ${e.message}" }
        }

        override fun streamReasoning(input: String, callback: IInferenceCallback) {
            serviceScope.launch {
                try {
                    executeInference(input).collect { token -> callback.onToken(token, false) }
                    callback.onToken("", true)
                } catch (e: Exception) { callback.onError(e.message ?: "Inference Fault") }
            }
        }

        override fun isHydrated(): Boolean = litertEngine != null
        override fun getActiveModelPath(): String = currentModelPath
        override fun notifyTrimMemory(level: Int) { if (level >= 80) resetConversationInternal() }
        override fun setSafeMode(enabled: Boolean) {}
        override fun isLowPerformanceMode(): Boolean = false
        override fun resetConversation() { 
            lastSummary = null
            resetConversationInternal() 
        }

        override fun summarizeAndReset(): String = runBlocking(Dispatchers.IO) {
            inferenceMutex.withLock {
            val activeConv = synchronized(this@InferenceService) { litertConversation } ?: return@withLock ""
            val summaryPrompt = "[INTERNAL] Summarize our conversation concisely in 3 lines of Myanmar text. Focus on facts. No thinking tags."
            val fullResult = StringBuilder()
            try {
                activeConv.sendMessageAsync(Message.user(summaryPrompt)).collect { partial ->
                    val token = try { 
                        val method = partial.javaClass.getMethod("getText")
                        method.invoke(partial) as String 
                    } catch(e: Exception) { partial.toString() }
                    fullResult.append(token)
                }
                val summary = fullResult.toString().replace("[REPLY]", "").replace("[/REPLY]", "").trim()
                lastSummary = summary
                resetConversationLocked()
                Log.i(TAG, "Summarization Complete. Next turn will include context anchor.")
                summary
            } catch (e: Exception) { 
                Log.e(TAG, "Summarization Failed: ${e.message}")
                ""
            }
            }
        }

        override fun updateSamplingParams(temp: Float, topK: Int, topP: Float) {
            updateGenerationConfig(temp, topK, topP, currentMaxTokens)
        }

        override fun updateGenerationConfig(temp: Float, topK: Int, topP: Float, maxTokens: Int) {
            val nextMaxTokens = maxTokens.coerceIn(128, 4096)
            val requiresRehydrate = nextMaxTokens != currentMaxTokens && currentModelPath.isNotEmpty()

            currentTemp = temp.coerceAtLeast(0.0f)
            currentTopK = topK.coerceAtLeast(1)
            currentTopP = topP.coerceIn(0.0f, 1.0f)
            currentMaxTokens = nextMaxTokens

            if (requiresRehydrate) {
                serviceScope.launch { tryHydrate(currentModelPath) }
            } else if (litertEngine != null) {
                resetConversationInternal()
            }
        }

        override fun generateEmbeddingsBatch(texts: List<String>?, embeddingModelPath: String?): FloatArray {
            if (texts.isNullOrEmpty()) return FloatArray(0)
            // Enforce max 16 chunks and <= 128 KB UTF-8 byte budget per Binder IPC transaction
            var byteCount = 0
            val safeTexts = mutableListOf<String>()
            for (t in texts) {
                if (safeTexts.size >= 16) break
                val tBytes = t.toByteArray(Charsets.UTF_8).size
                if (safeTexts.isNotEmpty() && byteCount + tBytes > 128 * 1024) break
                byteCount += tBytes
                safeTexts.add(t)
            }
            return runBlocking(Dispatchers.IO) {
                computeEmbeddingsBatchInternal(safeTexts, embeddingModelPath ?: "")
            }
        }
    }

    companion object {
        const val EMBEDDING_DIM = 768

        init {
            try { System.loadLibrary("ronin_kernel") } catch (e: Throwable) {
                Log.e("RoninKernel", "Failed to load native library.")
            }
        }

        /**
         * Deterministic L2-normalized 768D multilingual feature-hash / n-gram embedding.
         * Deprecated in Phase 2: Do NOT inject non-neural hash vectors into neural embedding space.
         */
        @Deprecated("Do not use feature-hash fallback vectors in neural embedding space (Phase 2 Refinement)")
        fun computeDeterministic768Embedding(text: String): FloatArray {
            val vec = FloatArray(EMBEDDING_DIM)
            val normalized = text.lowercase().trim()
            if (normalized.isEmpty()) return vec

            // Character 2-grams, 3-grams, and word tokens hashed with FNV-1a into 768 dimensions
            val len = normalized.length
            for (i in 0 until len) {
                for (n in 1..3) {
                    if (i + n <= len) {
                        var hash = -0x7ee3623b
                        for (k in 0 until n) {
                            hash = hash xor normalized[i + k].code
                            hash *= 0x01000193
                        }
                        val idx = (hash and 0x7FFFFFFF) % EMBEDDING_DIM
                        val sign = if ((hash and 1) == 0) 1.0f else -1.0f
                        vec[idx] += sign * (if (n == 1) 0.5f else 1.0f)
                    }
                }
            }

            // L2 Normalize
            var sumSq = 0.0
            for (v in vec) sumSq += v.toDouble() * v.toDouble()
            if (sumSq > 1e-12) {
                val invNorm = (1.0 / kotlin.math.sqrt(sumSq)).toFloat()
                for (i in vec.indices) vec[i] *= invNorm
            }
            return vec
        }
    }

    private suspend fun computeEmbeddingsBatchInternal(texts: List<String>, embeddingModelPath: String): FloatArray {
        val embFile = if (embeddingModelPath.isNotBlank()) File(embeddingModelPath) else null
        val canUseLitertEmb = embFile != null && embFile.exists() && embFile.isFile

        if (!canUseLitertEmb) {
            // Zero feature-hash fallback: When neural embedding model is absent, return empty array
            // so callers cleanly fall back to SQLite FTS5 BM25 lexical search without vector space pollution.
            return FloatArray(0)
        }

        val result = FloatArray(texts.size * EMBEDDING_DIM)

        // Batch-First On-Demand Model Swapping with RAM Guard & Mutex Protection
        inferenceMutex.withLock {
            val previousModelPath = currentModelPath
            val freeRam = try { getFreeRamGBNative() } catch (_: Throwable) { 4.0f }
            val needsSwap = previousModelPath.isNotEmpty() && previousModelPath != embFile!!.absolutePath && freeRam < 2.2f

            var tempEngine: Engine? = null
            try {
                if (needsSwap) {
                    Log.i(TAG, "On-Demand RAG Swap: Releasing Reasoning Model ($previousModelPath) to load EmbeddingGemma 2 (${embFile!!.name}), FreeRAM=${"%.2f".format(freeRam)}GB")
                    releaseResourcesLocked()
                }
                val engineToUse = if (currentModelPath == embFile!!.absolutePath && litertEngine != null) {
                    litertEngine!!
                } else {
                    val cfg = EngineConfig(modelPath = embFile.absolutePath, maxNumTokens = 512)
                    Engine(cfg).also {
                        it.initialize()
                        tempEngine = it
                    }
                }

                // Attempt native LiteRT embedding extraction
                val embedMethod = try {
                    engineToUse.javaClass.methods.firstOrNull {
                        it.name.contains("embed", ignoreCase = true) && it.parameterCount == 1
                    }
                } catch (_: Throwable) { null }

                for (i in texts.indices) {
                    val rawVec = try {
                        val invoked = embedMethod?.invoke(engineToUse, texts[i])
                        when (invoked) {
                            is FloatArray -> invoked
                            is List<*> -> invoked.filterIsInstance<Number>().map { it.toFloat() }.toFloatArray()
                            else -> null
                        }
                    } catch (_: Throwable) { null }

                    if (rawVec != null && rawVec.size == EMBEDDING_DIM) {
                        var sumSq = 0.0
                        for (v in rawVec) sumSq += v.toDouble() * v.toDouble()
                        val finalVec = if (sumSq > 1e-12) {
                            val inv = (1.0 / kotlin.math.sqrt(sumSq)).toFloat()
                            FloatArray(EMBEDDING_DIM) { idx -> rawVec[idx] * inv }
                        } else rawVec
                        System.arraycopy(finalVec, 0, result, i * EMBEDDING_DIM, EMBEDDING_DIM)
                    } else {
                        Log.w(TAG, "LiteRT embedding extraction failed for chunk $i; falling back cleanly to lexical search")
                        return FloatArray(0)
                    }
                }
            } catch (t: Throwable) {
                Log.w(TAG, "EmbeddingGemma 2 on-demand pass failed: ${t.message}")
                return FloatArray(0)
            } finally {
                try { tempEngine?.close() } catch (_: Throwable) {}
            }
        }
        return result
    }

    override fun onBind(intent: Intent?): IBinder = binder

    private fun tryHydrate(path: String): String {
        val response = JSONObject()
        if (!File(path).exists()) {
            response.put("success", false)
            response.put("error", JSONObject().put("code", "MODEL_MISSING").put("message", "Model file not found at $path"))
            return response.toString()
        }
        return runBlocking(Dispatchers.IO) {
            inferenceMutex.withLock {
                releaseResourcesLocked()
                try {
                    // Keep the engine token window aligned with the UI generation config.
                    val config = EngineConfig(
                        modelPath = path, 
                        maxNumTokens = currentMaxTokens
                    )
                    val engine = Engine(config)
                    engine.initialize()
                    litertEngine = engine
                    litertConversation = createConversationLocked()
                    isConversationFresh = true; currentModelPath = path
                    Log.i(TAG, "Hardened Spine Hydrated (MaxTokens=$currentMaxTokens, T=$currentTemp, P=$currentTopP, K=$currentTopK): $path")
                    response.put("success", true)
                    response.toString()
                } catch (e: Throwable) { 
                    Log.e(TAG, "Hydration Failed: ${e.message}")
                    // v1.5 Self-Healing: If hydration fails, release to prevent zombie state
                    releaseResourcesLocked()
                    response.put("success", false)
                    response.put("error", JSONObject().put("code", "HYDRATION_FAILED").put("message", e.message ?: "LiteRT init error"))
                    response.toString()
                }
            }
        }
    }

    private fun releaseResourcesLocked() {
        synchronized(this) {
            try { 
                litertConversation?.close(); litertConversation = null
                litertEngine?.close(); litertEngine = null 
                Log.w(TAG, "Neural resources released due to instability.")
            } catch (e: Exception) {}
        }
    }

    private fun releaseResources() {
        runBlocking(Dispatchers.IO) {
            inferenceMutex.withLock {
                releaseResourcesLocked()
            }
        }
    }

    private fun executeInference(input: String): Flow<String> = flow {
        // v1.5 Self-Healing: Check and auto-hydrate if engine was released due to instability
        if (litertEngine == null && currentModelPath.isNotEmpty()) {
            Log.i(TAG, "Spine is dry. Attempting Auto-Rehydration before inference...")
            val hydRes = tryHydrate(currentModelPath)
            if (!hydRes.contains("\"success\":true")) {
                emit("Error: Hydration Failed. Engine could not be initialized.")
                return@flow
            }
        }

        inferenceMutex.withLock {
        // v7.1: Selective Stripping Hardening
        // We MUST preserve instructions for internal system calls (Summarization, Planning).
        val isInternalCall = input.contains("[INTERNAL]") || input.contains("Task Planner") || input.contains("CORE_IDENTITY")
        
        var userPrompt = if (!isConversationFresh && input.contains("User: ") && !isInternalCall) {
            input.substringAfter("User: ").trim()
        } else {
            input
        }
        
        // Inject Summary as context anchor if turn 1
        if (isConversationFresh && lastSummary != null) {
            userPrompt = "CONTEXT_ANCHOR (Previous Conversation): ${lastSummary}\n\n$userPrompt"
            Log.d(TAG, "Injected Summary Anchor into Turn 1.")
        }
        
        // v6.1 Maximum RAM Guard: 0.8GB (Mid-range protection)
        val freeRam = try { getFreeRamGBNative() } catch (e: Exception) { 4.0f }
        if (freeRam < 0.8f && !isConversationFresh) {
            Log.w(TAG, "CRITICAL RAM ALERT (%.2fGB). Purging KV Cache.".format(freeRam))
            resetConversationLocked()
        }

        val activeConv = synchronized(this@InferenceService) { litertConversation }
        if (activeConv == null) {
            emit("Error: Active Conversation is null.")
            return@withLock
        }
        
        // Mark as no longer fresh AFTER we have decided to use/strip the wrap for THIS prompt
        val wasFresh = isConversationFresh
        isConversationFresh = false 
        
        try {
            Log.d(TAG, "Inference active (Fresh=$wasFresh). Prompt length: ${userPrompt.length}")
            activeConv.sendMessageAsync(Message.user(userPrompt)).collect { partial ->
                emit(extractTokenText(partial))
            }
        } catch (e: Exception) {
            Log.e(TAG, "LiteRT Fault: ${e.message}")
            if (e.message?.contains("Status Code: 13") == true || e.message?.contains("failed to invoke", ignoreCase = true) == true) {
                Log.w(TAG, "Terminally unstable engine state detected (Error 13). Forcing Hard Reset...")
                releaseResourcesLocked()
            } else if (e.message?.contains("not alive", ignoreCase = true) == true || e.message?.contains("failed", ignoreCase = true) == true) {
                Log.w(TAG, "Attempting to recover from dead conversation...")
                resetConversationLocked()
            }
            throw e
        }
        }
    }.flowOn(Dispatchers.IO)

    private fun extractTokenText(partial: Any): String {
        if (!getTextLookupDone) {
            synchronized(this) {
                if (!getTextLookupDone) {
                    try {
                        val m = partial.javaClass.getMethod("getText")
                        m.isAccessible = true
                        cachedGetTextMethod = m
                    } catch (_: Throwable) {
                        cachedGetTextMethod = null
                    } finally {
                        getTextLookupDone = true
                    }
                }
            }
        }
        val method = cachedGetTextMethod
        return if (method != null) {
            try {
                method.invoke(partial) as String
            } catch (_: Throwable) {
                partial.toString()
            }
        } else {
            partial.toString()
        }
    }

    override fun onDestroy() { super.onDestroy(); releaseResources(); try { shutdownKernelNative() } catch (e: Exception) {} }
}
