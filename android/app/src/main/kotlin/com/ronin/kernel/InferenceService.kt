package com.ronin.kernel

import android.app.ActivityManager
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
import org.json.JSONArray
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

    @Volatile
    private var lastEmbeddingError: String = ""
    @Volatile
    private var visionGpuBlocked: Boolean = false

    private var cachedEmbeddingEngine: EmbeddingEngine? = null
    private var cachedEmbeddingModelPath: String = ""
    private var cachedEmbVisionEnabled: Boolean = false
    private var cachedEmbAudioEnabled: Boolean = false
    private var cachedEmbMainIsGpu: Boolean = false
    private var cachedEmbVisionIsGpu: Boolean = false
    private var cachedEmbVisionSignatures: IntArray = IntArray(0)
    private var cachedEmbMaxVisionBudget: Int = 0
    private var cachedEmbConfigVisionTokens: Int? = null
    private var cachedEmbMaxInputLength: Int? = null
    private val verifiedVisionProfiles = HashSet<String>()
    
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
        override fun notifyTrimMemory(level: Int) {
            if (level >= 80) {
                resetConversationInternal()
                serviceScope.launch {
                    inferenceMutex.withLock {
                        releaseEmbeddingEngineLocked()
                    }
                }
            }
        }
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
                    fullResult.append(extractTokenText(partial))
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
            return generateEmbeddingsBatchWithTask(texts, TASK_RAW, "", embeddingModelPath)
        }

        override fun generateEmbeddingsBatchWithTask(
            texts: List<String>?,
            taskMode: String?,
            titleHint: String?,
            embeddingModelPath: String?
        ): FloatArray {
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
                computeTextEmbeddingsBatchInternal(
                    texts = safeTexts,
                    taskMode = taskMode ?: TASK_RAW,
                    titleHint = titleHint ?: "",
                    embeddingModelPath = embeddingModelPath ?: ""
                )
            }
        }

        override fun generateMultimodalEmbeddingsBatch(
            mediaTypes: List<String>?,
            payloads: List<String>?,
            visionTokensPerImage: Int,
            embeddingModelPath: String?
        ): FloatArray {
            if (mediaTypes.isNullOrEmpty() || payloads.isNullOrEmpty() || mediaTypes.size != payloads.size) {
                return FloatArray(0)
            }
            val limit = minOf(mediaTypes.size, 16)
            val safeTypes = mediaTypes.take(limit)
            val safePayloads = payloads.take(limit)
            return runBlocking(Dispatchers.IO) {
                computeMultimodalEmbeddingsBatchInternal(
                    mediaTypes = safeTypes,
                    payloads = safePayloads,
                    visionTokensPerImage = visionTokensPerImage,
                    embeddingModelPath = embeddingModelPath ?: ""
                )
            }
        }

        override fun inspectEmbeddingModel(embeddingModelPath: String?): String {
            return inspectEmbeddingModelInternal(embeddingModelPath ?: "")
        }

        override fun releaseEmbeddingEngine() {
            runBlocking(Dispatchers.IO) {
                inferenceMutex.withLock {
                    releaseEmbeddingEngineLocked()
                }
            }
        }

        override fun getLastEmbeddingError(): String = lastEmbeddingError
    }

    companion object {
        const val EMBEDDING_DIM = 768
        const val LITERT_LM_RUNTIME_VERSION = "0.19.1"
        const val TASK_QUERY = "QUERY"
        const val TASK_DOCUMENT = "DOCUMENT"
        const val TASK_NONE = "NONE"
        const val TASK_RAW = "RAW"
        const val MEDIA_VISION_TOKENS_PER_IMAGE = 70
        const val MEDIA_MAX_INPUT_LENGTH = 256

        /**
         * Builds an isolated LiteRT-LM cache subdirectory name partitioned by model filename,
         * active modality backends, configured vision tokens per image, and max input sequence length.
         */
        fun buildEmbeddingCacheSubdirName(
            modelName: String,
            hasVision: Boolean,
            visionIsGpu: Boolean,
            hasAudio: Boolean,
            visionTokensPerImage: Int?,
            maxInputLength: Int?,
            mainIsGpu: Boolean = false
        ): String {
            val safeModel = modelName
                .trim()
                .lowercase()
                .replace(Regex("[^a-z0-9._-]+"), "_")
                .ifEmpty { "embedding_model" }
            val visionTag = when {
                !hasVision -> "v0"
                visionIsGpu -> "vgpu"
                else -> "vcpu"
            }
            val audioTag = if (hasAudio) "a1" else "a0"
            val vtTag = "vt${visionTokensPerImage?.takeIf { it > 0 }?.toString() ?: "def"}"
            val lenTag = "len${maxInputLength?.takeIf { it > 0 }?.toString() ?: "def"}"
            val mainSuffix = if (mainIsGpu) "_mgpu" else ""
            return "emb_cache_${safeModel}_${visionTag}_${audioTag}_${vtTag}_${lenTag}${mainSuffix}"
        }

        /**
         * Determines whether a cached [EmbeddingEngine] instance satisfies a new request profile
         * without reloading the model weights or mixing incompatible sequence/vision budgets.
         */
        fun shouldReuseCachedEmbeddingEngine(
            sameModelPath: Boolean,
            requireVision: Boolean,
            requireAudio: Boolean,
            forceVisionCpu: Boolean,
            cachedVisionEnabled: Boolean,
            cachedAudioEnabled: Boolean,
            cachedVisionIsGpu: Boolean,
            cachedConfigVisionTokens: Int?,
            cachedMaxInputLength: Int?,
            requestedVisionTokens: Int?,
            requestedMaxInputLength: Int?,
            cachedMainIsGpu: Boolean = cachedVisionIsGpu
        ): Boolean {
            if (!sameModelPath) return false
            if (cachedVisionEnabled && cachedVisionIsGpu != cachedMainIsGpu) return false
            if (forceVisionCpu && (cachedVisionIsGpu || cachedMainIsGpu)) return false
            if (requireAudio && cachedMainIsGpu) return false
            val visionOk = !requireVision || (cachedVisionEnabled && (!forceVisionCpu || !cachedVisionIsGpu))
            val audioOk = !requireAudio || cachedAudioEnabled
            val maxLenOk = cachedMaxInputLength == requestedMaxInputLength
            val tokensOk = (!requireVision && !cachedVisionEnabled) ||
                requestedVisionTokens == null ||
                cachedConfigVisionTokens == requestedVisionTokens
            return visionOk && audioOk && maxLenOk && tokensOk
        }

        /**
         * Returns true when [vec] is non-empty, contains only finite floats, and has squared L2 norm > 1e-8f.
         */
        internal fun isValidNonZeroFiniteVector(vec: FloatArray): Boolean {
            if (vec.isEmpty()) return false
            var sumSq = 0.0
            for (v in vec) {
                if (!v.isFinite()) return false
                sumSq += v.toDouble() * v.toDouble()
            }
            return sumSq.isFinite() && sumSq > 1e-8f
        }

        /**
         * Computes cosine similarity between [a] and [b], returning 0f if either vector is invalid or sizes differ.
         */
        internal fun cosineSimilarity(a: FloatArray, b: FloatArray): Float {
            if (a.size != b.size || !isValidNonZeroFiniteVector(a) || !isValidNonZeroFiniteVector(b)) {
                return 0f
            }
            var dot = 0.0
            var normA = 0.0
            var normB = 0.0
            for (i in a.indices) {
                val va = a[i].toDouble()
                val vb = b[i].toDouble()
                dot += va * vb
                normA += va * va
                normB += vb * vb
            }
            val denom = kotlin.math.sqrt(normA * normB)
            if (!denom.isFinite() || denom <= 0.0) return 0f
            return (dot / denom).toFloat().coerceIn(-1f, 1f)
        }

        /**
         * Returns true when either vision embedding is invalid, dimensions differ, or |cosine| >= [maxAllowedCosine].
         */
        internal fun isVisionPairCollapsed(
            vecA: FloatArray,
            vecB: FloatArray,
            maxAllowedCosine: Float = 0.995f
        ): Boolean {
            if (vecA.size != vecB.size || !isValidNonZeroFiniteVector(vecA) || !isValidNonZeroFiniteVector(vecB)) {
                return true
            }
            return kotlin.math.abs(cosineSimilarity(vecA, vecB)) >= maxAllowedCosine
        }

        /**
         * Generates deterministic high-contrast synthetic BGRA TGA probes for vision backend self-tests.
         */
        internal fun buildSyntheticContrastProbeTga(variant: Boolean, size: Int = 64): ByteArray {
            require(size > 0) { "Probe size must be positive: $size" }
            val pixels = IntArray(size * size)
            if (!variant) {
                val darkBlue = 0xFF050814.toInt()
                val cyan = 0xFF00D8FF.toInt()
                val maxX = (size - 1).coerceAtLeast(1)
                for (y in 0 until size) {
                    for (x in 0 until size) {
                        val base = if ((x / 8) % 2 == 0) darkBlue else cyan
                        val grad = (x * 255) / maxX
                        val r = (((base ushr 16) and 0xFF) + grad / 4).coerceIn(0, 255)
                        val g = (base ushr 8) and 0xFF
                        val b = ((base and 0xFF) - grad / 8).coerceIn(0, 255)
                        pixels[y * size + x] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
                    }
                }
            } else {
                val redOrange = 0xFFFF2A00.toInt()
                val yellow = 0xFFFFE600.toInt()
                val white = 0xFFFFFFFF.toInt()
                val centerStart = size / 4
                val centerEnd = (size * 3) / 4
                for (y in 0 until size) {
                    for (x in 0 until size) {
                        val inCenter = x in centerStart until centerEnd && y in centerStart until centerEnd
                        pixels[y * size + x] = if (inCenter) {
                            white
                        } else if (((x / 8) + (y / 8)) % 2 == 0) {
                            redOrange
                        } else {
                            yellow
                        }
                    }
                }
            }
            return TgaImageCodec.encodeArgbPixelsToBgraTga(pixels, size, size)
        }

        /**
         * Parses semantic version strings safely and returns true when [currentVersion]
         * satisfies [minRequiredVersion]. Blank/missing requirements are treated as compatible.
         */
        fun isRuntimeVersionCompatible(currentVersion: String, minRequiredVersion: String): Boolean {
            val required = minRequiredVersion.trim()
            if (required.isEmpty()) return true
            val current = currentVersion.trim()
            if (current.isEmpty()) return false

            fun parseSemVer(raw: String): Pair<List<Int>, Boolean>? {
                val cleaned = raw.removePrefix("v").removePrefix("V").substringBefore('+').trim()
                if (cleaned.isEmpty()) return null
                val dashIdx = cleaned.indexOf('-')
                val corePart = if (dashIdx >= 0) cleaned.substring(0, dashIdx) else cleaned
                val hasPreRelease = dashIdx >= 0 && cleaned.substring(dashIdx + 1).isNotBlank()
                if (corePart.isBlank()) return null
                val segments = corePart.split('.').map { seg ->
                    val digits = seg.takeWhile { it.isDigit() }
                    digits.toIntOrNull() ?: return null
                }
                return segments to hasPreRelease
            }

            val (curNums, curPre) = parseSemVer(current) ?: return false
            val (reqNums, reqPre) = parseSemVer(required) ?: return true
            val maxLen = maxOf(curNums.size, reqNums.size, 3)
            for (i in 0 until maxLen) {
                val c = curNums.getOrElse(i) { 0 }
                val r = reqNums.getOrElse(i) { 0 }
                if (c > r) return true
                if (c < r) return false
            }
            return !(curPre && !reqPre)
        }

        /**
         * Formats text queries and corpus chunks with official EmbeddingGemma 2 asymmetric prefixes.
         * Note: Cross-modal media queries (TASK_NONE) and raw inputs (TASK_RAW) must pass raw trimmed text
         * without text-retrieval prefixes so text queries align directly with image/video/audio embeddings.
         */
        fun formatForEmbeddingGemma2(text: String, taskMode: String, titleHint: String = ""): String {
            val clean = text.trim()
            return when (taskMode.uppercase()) {
                TASK_QUERY -> "task: search result | query: $clean"
                TASK_DOCUMENT -> {
                    val safeTitle = titleHint.trim().ifEmpty { "none" }
                    "title: $safeTitle | text: $clean"
                }
                TASK_NONE, TASK_RAW -> clean
                else -> clean
            }
        }

        /**
         * Selects a valid per-image vision token budget matching the model's vision signatures and max budget.
         */
        fun selectVisionTokensPerImage(requested: Int, supportedSignatures: IntArray, maxBudget: Int): Int? {
            if (requested <= 0) return null
            val validSigs = supportedSignatures.filter { it > 0 }.sorted()
            if (validSigs.isNotEmpty()) {
                val bounded = if (maxBudget > 0) validSigs.filter { it <= maxBudget } else validSigs
                val pool = if (bounded.isNotEmpty()) bounded else validSigs
                return pool.lastOrNull { it <= requested } ?: pool.first()
            }
            return if (maxBudget > 0) minOf(requested, maxBudget) else null
        }

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

    private fun getWorkerAvailableRamGB(): Float {
        return try {
            val am = getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
            if (am != null) {
                val mi = ActivityManager.MemoryInfo()
                am.getMemoryInfo(mi)
                mi.availMem / (1024f * 1024f * 1024f)
            } else {
                4.0f
            }
        } catch (_: Throwable) {
            4.0f
        }
    }

    private fun l2NormalizeCopy(rawVec: FloatArray): FloatArray {
        var sumSq = 0.0
        for (v in rawVec) sumSq += v.toDouble() * v.toDouble()
        return if (sumSq > 1e-12) {
            val inv = (1.0 / kotlin.math.sqrt(sumSq)).toFloat()
            FloatArray(rawVec.size) { idx -> rawVec[idx] * inv }
        } else {
            rawVec.copyOf()
        }
    }

    private fun releaseEmbeddingEngineLocked() {
        synchronized(this) {
            val eng = cachedEmbeddingEngine
            cachedEmbeddingEngine = null
            cachedEmbeddingModelPath = ""
            cachedEmbVisionEnabled = false
            cachedEmbAudioEnabled = false
            cachedEmbMainIsGpu = false
            cachedEmbVisionIsGpu = false
            cachedEmbVisionSignatures = IntArray(0)
            cachedEmbMaxVisionBudget = 0
            cachedEmbConfigVisionTokens = null
            cachedEmbMaxInputLength = null
            if (eng != null) {
                try {
                    if (eng.isInitialized()) eng.close()
                } catch (e: Throwable) {
                    Log.w(TAG, "EmbeddingEngine close notice: ${e.message}")
                }
            }
        }
    }

    private fun inspectEmbeddingModelInternal(embeddingModelPath: String): String {
        val out = JSONObject()
        val embFile = if (embeddingModelPath.isNotBlank()) File(embeddingModelPath) else null
        if (embFile == null || !embFile.exists() || !embFile.isFile) {
            out.put("exists", false)
            out.put("is_embedding", false)
            out.put("error", "Model file not found")
            return out.toString()
        }
        return try {
            ModelInfo.from(embFile.absolutePath).use { info ->
                val mods = info.inputModalities()
                val isEmb = info.modelType == ModelType.EMBEDDING
                val reportedDim = (info as? ModelInfo.Embedding)?.dimension() ?: 0
                val dim = if (reportedDim > 0) reportedDim else EMBEDDING_DIM
                val dimCompatible = reportedDim <= 0 || reportedDim == EMBEDDING_DIM
                val visionSigs = runCatching { info.visionSignatureSelection() }.getOrNull() ?: IntArray(0)
                val visionGpuSupported = mods.vision && runCatching {
                    info.supportedBackends(Modality.VISION).contains(BackendType.GPU)
                }.getOrDefault(false)
                val minRuntimeVer = runCatching { info.minRuntimeVersion() }.getOrDefault("") ?: ""
                val runtimeCompatible = isRuntimeVersionCompatible(LITERT_LM_RUNTIME_VERSION, minRuntimeVer)
                if (!runtimeCompatible) {
                    Log.w(TAG, "Model ${embFile.name} requires LiteRT-LM >= $minRuntimeVer (runtime=$LITERT_LM_RUNTIME_VERSION)")
                }
                if (!dimCompatible) {
                    Log.w(TAG, "Model ${embFile.name} embedding dim=$reportedDim differs from expected $EMBEDDING_DIM")
                }
                val sigArray = JSONArray()
                for (sig in visionSigs) sigArray.put(sig)

                out.put("exists", true)
                out.put("model_path", embFile.absolutePath)
                out.put("model_name", embFile.name)
                out.put("is_embedding", isEmb)
                out.put("model_type", info.modelType.name)
                out.put("dimension", dim)
                out.put("dimension_compatible", dimCompatible)
                out.put("text", mods.text)
                out.put("vision", mods.vision)
                out.put("audio", mods.audio)
                out.put("video", mods.video)
                out.put("max_context_tokens", info.maxContextTokens())
                out.put("max_vision_token_budget", info.maxVisionTokenBudget())
                out.put("vision_signatures", sigArray)
                out.put("vision_gpu_supported", visionGpuSupported)
                out.put("min_runtime_version", minRuntimeVer)
                out.put("runtime_version", LITERT_LM_RUNTIME_VERSION)
                out.put("runtime_compatible", runtimeCompatible)
            }
            out.toString()
        } catch (t: Throwable) {
            Log.w(TAG, "ModelInfo inspection failed for ${embFile.name}: ${t.message}")
            out.put("exists", true)
            out.put("is_embedding", false)
            out.put("error", t.message ?: "ModelInfo failed")
            out.toString()
        }
    }

    private fun getOrCreateEmbeddingEngineLocked(
        embFile: File,
        requireVision: Boolean,
        requireAudio: Boolean,
        forceVisionCpu: Boolean = false,
        visionTokensPerImage: Int? = null,
        maxInputLength: Int? = null
    ): EmbeddingEngine? {
        val existing = cachedEmbeddingEngine
        if (existing != null && existing.isInitialized() &&
            shouldReuseCachedEmbeddingEngine(
                sameModelPath = cachedEmbeddingModelPath == embFile.absolutePath,
                requireVision = requireVision,
                requireAudio = requireAudio,
                forceVisionCpu = forceVisionCpu,
                cachedVisionEnabled = cachedEmbVisionEnabled,
                cachedAudioEnabled = cachedEmbAudioEnabled,
                cachedVisionIsGpu = cachedEmbVisionIsGpu,
                cachedConfigVisionTokens = cachedEmbConfigVisionTokens,
                cachedMaxInputLength = cachedEmbMaxInputLength,
                requestedVisionTokens = visionTokensPerImage,
                requestedMaxInputLength = maxInputLength,
                cachedMainIsGpu = cachedEmbMainIsGpu
            )
        ) {
            return existing
        }

        val prevSameModel = cachedEmbeddingModelPath == embFile.absolutePath
        val prevVision = prevSameModel && cachedEmbVisionEnabled && !forceVisionCpu
        val prevAudio = prevSameModel && cachedEmbAudioEnabled
        releaseEmbeddingEngineLocked()

        val availRamGb = getWorkerAvailableRamGB()
        if (litertEngine != null && (requireVision || requireAudio || availRamGb < 3.2f)) {
            Log.i(TAG, "On-Demand RAG Swap: Releasing Reasoning Model ($currentModelPath) before ${embFile.name} (vision=$requireVision, audio=$requireAudio, availRAM=${"%.2f".format(availRamGb)}GB)")
            releaseResourcesLocked()
        }

        var supportsVision = requireVision
        var supportsAudio = requireAudio
        var mainGpuSupported = false
        var visionGpuSupported = false
        var visionSignatures = IntArray(0)
        var maxVisionBudget = 0
        var compatibilityError: String? = null
        try {
            ModelInfo.from(embFile.absolutePath).use { info ->
                if (info.modelType != ModelType.EMBEDDING) {
                    Log.w(TAG, "Model ${embFile.name} reported ModelType=${info.modelType} instead of EMBEDDING")
                }
                val minRuntimeVer = runCatching { info.minRuntimeVersion() }.getOrDefault("") ?: ""
                if (!isRuntimeVersionCompatible(LITERT_LM_RUNTIME_VERSION, minRuntimeVer)) {
                    compatibilityError =
                        "Model '${embFile.name}' requires LiteRT-LM runtime >= $minRuntimeVer (installed: $LITERT_LM_RUNTIME_VERSION)."
                }
                val reportedDim = (info as? ModelInfo.Embedding)?.dimension() ?: 0
                if (compatibilityError == null && reportedDim > 0 && reportedDim != EMBEDDING_DIM) {
                    compatibilityError =
                        "Model '${embFile.name}' embedding dimension $reportedDim is incompatible with expected $EMBEDDING_DIM."
                }
                val mods = info.inputModalities()
                supportsVision = mods.vision
                supportsAudio = mods.audio
                visionSignatures = runCatching { info.visionSignatureSelection() }.getOrNull() ?: IntArray(0)
                maxVisionBudget = runCatching { info.maxVisionTokenBudget() }.getOrDefault(0)
                mainGpuSupported = runCatching {
                    info.supportedBackends(Modality.TEXT).contains(BackendType.GPU)
                }.getOrDefault(false)
                visionGpuSupported = mods.vision && runCatching {
                    info.supportedBackends(Modality.VISION).contains(BackendType.GPU)
                }.getOrDefault(false)
            }
        } catch (t: Throwable) {
            Log.w(TAG, "ModelInfo fallback for ${embFile.name}: ${t.message}")
        }

        val compatErr = compatibilityError
        if (compatErr != null) {
            lastEmbeddingError = compatErr
            Log.w(TAG, lastEmbeddingError)
            return null
        }

        if (requireVision && !supportsVision) {
            lastEmbeddingError = "Model '${embFile.name}' has no vision encoder (vision=$supportsVision, audio=$supportsAudio). Download EmbeddingGemma 2 740M (Omnimodal)."
            Log.w(TAG, lastEmbeddingError)
            return null
        }
        if (requireAudio && !supportsAudio) {
            lastEmbeddingError = "Model '${embFile.name}' has no audio encoder (vision=$supportsVision, audio=$supportsAudio). Download EmbeddingGemma 2 740M (Omnimodal)."
            Log.w(TAG, lastEmbeddingError)
            return null
        }

        val enableVision = requireVision || (prevVision && supportsVision && maxInputLength == null)
        val enableAudio = requireAudio || (prevAudio && supportsAudio && maxInputLength == null)
        val requestedVisionTokens = visionTokensPerImage?.takeIf { it > 0 }?.let { req ->
            selectVisionTokensPerImage(req, visionSignatures, maxVisionBudget) ?: req
        }
        val effectiveConfigVisionTokens = if (enableVision) requestedVisionTokens else null
        val effectiveMaxInputLen = maxInputLength?.takeIf { it > 0 }

        var activeConfigVisionTokens: Int? = effectiveConfigVisionTokens
        var activeMaxInputLen: Int? = effectiveMaxInputLen

        fun tryInitEngine(
            mBackend: Backend,
            vBackend: Backend?,
            aBackend: Backend?,
            cfgVisionTokens: Int?,
            cfgMaxInputLen: Int?
        ): EmbeddingEngine {
            val useMainGpu = mBackend is Backend.GPU
            val subDir = buildEmbeddingCacheSubdirName(
                modelName = embFile.name,
                hasVision = vBackend != null,
                visionIsGpu = vBackend is Backend.GPU,
                hasAudio = aBackend != null,
                visionTokensPerImage = cfgVisionTokens,
                maxInputLength = cfgMaxInputLen,
                mainIsGpu = useMainGpu
            )
            val isolatedCacheDir = File(cacheDir, subDir).apply { mkdirs() }
            val cfg = EmbeddingEngineConfig(
                modelPath = embFile.absolutePath,
                backend = mBackend,
                visionBackend = vBackend,
                audioBackend = aBackend,
                cacheDir = isolatedCacheDir.absolutePath,
                maxInputLength = cfgMaxInputLen,
                visionTokensPerImage = cfgVisionTokens
            )
            val eng = EmbeddingEngine(cfg)
            return try {
                eng.initialize()
                eng
            } catch (t: Throwable) {
                try {
                    if (eng.isInitialized()) eng.close()
                } catch (_: Throwable) {}
                throw t
            }
        }

        fun tryInitWithOptionalProfileFallback(
            mBackend: Backend,
            vBackend: Backend?,
            aBackend: Backend?
        ): EmbeddingEngine {
            return try {
                activeConfigVisionTokens = effectiveConfigVisionTokens
                activeMaxInputLen = effectiveMaxInputLen
                tryInitEngine(mBackend, vBackend, aBackend, effectiveConfigVisionTokens, effectiveMaxInputLen)
            } catch (cfgErr: Throwable) {
                if (effectiveConfigVisionTokens != null || effectiveMaxInputLen != null) {
                    Log.w(
                        TAG,
                        "Engine init rejected explicit profile (visionTokens=$effectiveConfigVisionTokens, maxInputLen=$effectiveMaxInputLen): ${cfgErr.message}; retrying defaults"
                    )
                    activeConfigVisionTokens = null
                    activeMaxInputLen = null
                    tryInitEngine(mBackend, vBackend, aBackend, null, null)
                } else {
                    throw cfgErr
                }
            }
        }

        fun verifyVisionContrastIfNeeded(
            eng: EmbeddingEngine,
            actualMainGpu: Boolean,
            actualVisGpu: Boolean
        ) {
            if (!enableVision) return
            val profileKey = "${embFile.absolutePath}|m=$actualMainGpu|v=$actualVisGpu|t=$activeConfigVisionTokens|l=$activeMaxInputLen"
            if (verifiedVisionProfiles.contains(profileKey)) return

            val probeA = buildSyntheticContrastProbeTga(variant = false)
            val probeB = buildSyntheticContrastProbeTga(variant = true)
            val opts = EmbeddingOptions(
                normalize = true,
                insertSpecialTokens = true,
                visionTokensPerImage = activeConfigVisionTokens
            )
            val vecA = eng.computeEmbedding(listOf(InputData.Image(probeA)), opts).embedding
            val vecB = eng.computeEmbedding(listOf(InputData.Image(probeB)), opts).embedding
            val cos = cosineSimilarity(vecA, vecB)
            Log.i(
                TAG,
                "Vision contrast self-test ($profileKey): cos=${"%.5f".format(cos)} (dimA=${vecA.size}, dimB=${vecB.size})"
            )
            if (isVisionPairCollapsed(vecA, vecB, 0.995f)) {
                throw IllegalStateException(
                    "Vision backend collapsed synthetic contrast probe (cos=${"%.5f".format(cos)}, mainGpu=$actualMainGpu, visGpu=$actualVisGpu)"
                )
            }
            verifiedVisionProfiles.add(profileKey)
        }

        fun initAndVerifyEngine(
            mBackend: Backend,
            vBackend: Backend?,
            aBackend: Backend?
        ): EmbeddingEngine {
            val eng = tryInitWithOptionalProfileFallback(mBackend, vBackend, aBackend)
            return try {
                verifyVisionContrastIfNeeded(
                    eng = eng,
                    actualMainGpu = mBackend is Backend.GPU,
                    actualVisGpu = vBackend is Backend.GPU
                )
                eng
            } catch (t: Throwable) {
                try {
                    if (eng.isInitialized()) eng.close()
                } catch (_: Throwable) {}
                throw t
            }
        }

        val audioBackend = if (enableAudio) Backend.CPU() else null
        val useMatchedGpu = enableVision && !enableAudio && mainGpuSupported && visionGpuSupported && !forceVisionCpu && !visionGpuBlocked
        var actualMainGpu = false
        var actualVisGpu = false
        val initializedEngine: EmbeddingEngine? = try {
            if (useMatchedGpu) {
                try {
                    val eng = initAndVerifyEngine(Backend.GPU(), Backend.GPU(), audioBackend)
                    actualMainGpu = true
                    actualVisGpu = true
                    eng
                } catch (gpuErr: Throwable) {
                    visionGpuBlocked = true
                    Log.w(TAG, "Matched GPU vision backend failed/collapsed (${gpuErr.message}); retrying on CPU")
                    actualMainGpu = false
                    actualVisGpu = false
                    initAndVerifyEngine(Backend.CPU(), Backend.CPU(), audioBackend)
                }
            } else {
                val visionBackend = if (enableVision) Backend.CPU() else null
                actualMainGpu = false
                actualVisGpu = false
                initAndVerifyEngine(Backend.CPU(), visionBackend, audioBackend)
            }
        } catch (cpuErr: Throwable) {
            lastEmbeddingError = "Failed to initialize ${embFile.name}: ${cpuErr.javaClass.simpleName}: ${cpuErr.message}"
            Log.w(TAG, lastEmbeddingError)
            null
        }

        if (initializedEngine != null && initializedEngine.isInitialized()) {
            cachedEmbeddingEngine = initializedEngine
            cachedEmbeddingModelPath = embFile.absolutePath
            cachedEmbVisionEnabled = enableVision
            cachedEmbAudioEnabled = enableAudio
            cachedEmbMainIsGpu = actualMainGpu
            cachedEmbVisionIsGpu = actualVisGpu
            cachedEmbVisionSignatures = visionSignatures
            cachedEmbMaxVisionBudget = maxVisionBudget
            cachedEmbConfigVisionTokens = activeConfigVisionTokens
            cachedEmbMaxInputLength = activeMaxInputLen
            Log.i(
                TAG,
                "EmbeddingGemma 2 initialized: ${embFile.name} (vision=$enableVision mainGpu=$actualMainGpu visionGpu=$actualVisGpu audio=$enableAudio sigs=${visionSignatures.contentToString()} maxVision=$maxVisionBudget cfgTokens=$activeConfigVisionTokens maxInputLen=$activeMaxInputLen)"
            )
        }
        return initializedEngine
    }

    private suspend fun computeTextEmbeddingsBatchInternal(
        texts: List<String>,
        taskMode: String,
        titleHint: String,
        embeddingModelPath: String
    ): FloatArray {
        val embFile = if (embeddingModelPath.isNotBlank()) File(embeddingModelPath) else null
        val canUseLitertEmb = embFile != null && embFile.exists() && embFile.isFile
        if (!canUseLitertEmb) {
            lastEmbeddingError = "Embedding model file missing."
            return FloatArray(0)
        }

        lastEmbeddingError = ""
        val isMediaQueryProfile = taskMode.equals(TASK_NONE, ignoreCase = true) || taskMode.equals(TASK_RAW, ignoreCase = true)
        val reqMaxInputLength = if (isMediaQueryProfile) MEDIA_MAX_INPUT_LENGTH else null
        val reqVisionTokens = if (isMediaQueryProfile) MEDIA_VISION_TOKENS_PER_IMAGE else null
        val result = FloatArray(texts.size * EMBEDDING_DIM)
        inferenceMutex.withLock {
            try {
                val engine = getOrCreateEmbeddingEngineLocked(
                    embFile = embFile!!,
                    requireVision = false,
                    requireAudio = false,
                    visionTokensPerImage = reqVisionTokens,
                    maxInputLength = reqMaxInputLength
                ) ?: return FloatArray(0)
                val opts = EmbeddingOptions(
                    normalize = true,
                    insertSpecialTokens = true
                )
                val inputsList = texts.map { raw ->
                    listOf(InputData.Text(formatForEmbeddingGemma2(raw, taskMode, titleHint)))
                }
                val batchResponses = try {
                    engine.computeEmbeddingBatch(inputsList, opts)
                } catch (batchErr: Throwable) {
                    Log.w(TAG, "computeEmbeddingBatch fallback to single calls: ${batchErr.message}")
                    inputsList.map { single -> engine.computeEmbedding(single, opts) }
                }
                if (batchResponses.size != texts.size) {
                    lastEmbeddingError = "Unexpected text batch response size: ${batchResponses.size} != ${texts.size}"
                    Log.w(TAG, lastEmbeddingError)
                    return FloatArray(0)
                }
                for (i in batchResponses.indices) {
                    val rawVec = batchResponses[i].embedding
                    if (rawVec.size != EMBEDDING_DIM) {
                        lastEmbeddingError = "Unexpected embedding dim=${rawVec.size} (expected $EMBEDDING_DIM) at index $i"
                        Log.w(TAG, lastEmbeddingError)
                        return FloatArray(0)
                    }
                    val normalized = l2NormalizeCopy(rawVec)
                    System.arraycopy(normalized, 0, result, i * EMBEDDING_DIM, EMBEDDING_DIM)
                }
            } catch (t: Throwable) {
                lastEmbeddingError = "${embFile!!.name} text pass failed: ${t.javaClass.simpleName}: ${t.message}"
                Log.w(TAG, lastEmbeddingError)
                releaseEmbeddingEngineLocked()
                return FloatArray(0)
            }
        }
        return result
    }

    private suspend fun computeMultimodalEmbeddingsBatchInternal(
        mediaTypes: List<String>,
        payloads: List<String>,
        visionTokensPerImage: Int,
        embeddingModelPath: String
    ): FloatArray {
        val embFile = if (embeddingModelPath.isNotBlank()) File(embeddingModelPath) else null
        if (embFile == null || !embFile.exists() || !embFile.isFile) {
            lastEmbeddingError = "Multimodal embedding model file missing."
            return FloatArray(0)
        }

        val needVision = mediaTypes.any { it.equals("IMAGE_FILE", ignoreCase = true) }
        val needAudio = mediaTypes.any { it.equals("AUDIO_FILE", ignoreCase = true) }

        val preparedInputs = ArrayList<List<InputData>>(mediaTypes.size)
        val imagePayloadBytes = ArrayList<ByteArray?>(mediaTypes.size)
        for (i in mediaTypes.indices) {
            val mType = mediaTypes[i].uppercase()
            val payload = payloads[i]
            var rawImgBytes: ByteArray? = null
            val itemInputs: List<InputData> = when (mType) {
                "TEXT" -> listOf(InputData.Text(payload))
                "IMAGE_FILE" -> {
                    val imgFile = File(payload)
                    if (!imgFile.exists() || !imgFile.isFile) {
                        lastEmbeddingError = "Missing image file for embedding: $payload"
                        Log.w(TAG, lastEmbeddingError)
                        return FloatArray(0)
                    }
                    val bytes = imgFile.readBytes()
                    rawImgBytes = bytes
                    listOf(InputData.Image(bytes))
                }
                "AUDIO_FILE" -> {
                    val audFile = File(payload)
                    if (!audFile.exists() || !audFile.isFile) {
                        lastEmbeddingError = "Missing audio file for embedding: $payload"
                        Log.w(TAG, lastEmbeddingError)
                        return FloatArray(0)
                    }
                    listOf(InputData.Audio(audFile.readBytes()))
                }
                else -> {
                    lastEmbeddingError = "Unsupported mediaType for embedding: $mType"
                    Log.w(TAG, lastEmbeddingError)
                    return FloatArray(0)
                }
            }
            preparedInputs.add(itemInputs)
            imagePayloadBytes.add(rawImgBytes)
        }

        fun runBatchOnce(engine: EmbeddingEngine, resolvedTokens: Int?): FloatArray {
            val opts = EmbeddingOptions(
                normalize = true,
                insertSpecialTokens = true,
                visionTokensPerImage = resolvedTokens
            )
            val responses = if (needVision || needAudio) {
                preparedInputs.map { single -> engine.computeEmbedding(single, opts) }
            } else if (preparedInputs.size > 1) {
                try {
                    engine.computeEmbeddingBatch(preparedInputs, opts)
                } catch (batchErr: Throwable) {
                    Log.w(TAG, "Multimodal computeEmbeddingBatch fallback to single calls: ${batchErr.message}")
                    preparedInputs.map { single -> engine.computeEmbedding(single, opts) }
                }
            } else {
                listOf(engine.computeEmbedding(preparedInputs[0], opts))
            }
            if (responses.size != preparedInputs.size) {
                throw IllegalStateException("Multimodal batch response size mismatch: ${responses.size} != ${preparedInputs.size}")
            }
            val out = FloatArray(preparedInputs.size * EMBEDDING_DIM)
            var firstImgIdx = -1
            var secondImgIdx = -1
            var firstImgVec: FloatArray? = null
            var secondImgVec: FloatArray? = null
            for (i in responses.indices) {
                val rawVec = responses[i].embedding
                if (rawVec.size != EMBEDDING_DIM) {
                    throw IllegalStateException("Multimodal embedding dim mismatch: ${rawVec.size} != $EMBEDDING_DIM")
                }
                if (!isValidNonZeroFiniteVector(rawVec)) {
                    throw IllegalStateException("Multimodal embedding at index $i is zero or non-finite")
                }
                val normalized = l2NormalizeCopy(rawVec)
                if (imagePayloadBytes[i] != null) {
                    if (firstImgIdx < 0) {
                        firstImgIdx = i
                        firstImgVec = normalized
                    } else if (secondImgIdx < 0) {
                        secondImgIdx = i
                        secondImgVec = normalized
                    }
                }
                System.arraycopy(normalized, 0, out, i * EMBEDDING_DIM, EMBEDDING_DIM)
            }
            if (needVision && preparedInputs.size >= 2 &&
                firstImgIdx >= 0 && secondImgIdx >= 0 &&
                !imagePayloadBytes[firstImgIdx]!!.contentEquals(imagePayloadBytes[secondImgIdx]!!)
            ) {
                val pairCos = cosineSimilarity(firstImgVec!!, secondImgVec!!)
                if (pairCos >= 0.9995f) {
                    throw IllegalStateException(
                        "Vision batch collapsed distinct images to identical vector (cos=${"%.5f".format(pairCos)})"
                    )
                }
            }
            return out
        }

        lastEmbeddingError = ""
        val requestedCfgTokens = if (needVision) {
            if (visionTokensPerImage > 0) visionTokensPerImage else MEDIA_VISION_TOKENS_PER_IMAGE
        } else {
            null
        }
        inferenceMutex.withLock {
            var lastErr: Throwable? = null
            val firstEngine = getOrCreateEmbeddingEngineLocked(
                embFile = embFile,
                requireVision = needVision,
                requireAudio = needAudio,
                forceVisionCpu = false,
                visionTokensPerImage = requestedCfgTokens,
                maxInputLength = MEDIA_MAX_INPUT_LENGTH
            ) ?: return FloatArray(0)
            val firstTokens = if (needVision) {
                selectVisionTokensPerImage(
                    requestedCfgTokens ?: MEDIA_VISION_TOKENS_PER_IMAGE,
                    cachedEmbVisionSignatures,
                    cachedEmbMaxVisionBudget
                )
            } else null

            try {
                return runBatchOnce(firstEngine, firstTokens)
            } catch (t: Throwable) {
                lastErr = t
                Log.w(TAG, "Multimodal pass failed (mainGpu=$cachedEmbMainIsGpu visionGpu=$cachedEmbVisionIsGpu tokens=$firstTokens): ${t.message}")
            }

            val firstUsedGpu = cachedEmbVisionIsGpu || cachedEmbMainIsGpu
            if (needVision && firstUsedGpu) {
                visionGpuBlocked = true
                releaseEmbeddingEngineLocked()
                val cpuEngine = getOrCreateEmbeddingEngineLocked(
                    embFile = embFile,
                    requireVision = true,
                    requireAudio = needAudio,
                    forceVisionCpu = true,
                    visionTokensPerImage = requestedCfgTokens,
                    maxInputLength = MEDIA_MAX_INPUT_LENGTH
                )
                if (cpuEngine != null) {
                    val cpuTokens = selectVisionTokensPerImage(
                        requestedCfgTokens ?: MEDIA_VISION_TOKENS_PER_IMAGE,
                        cachedEmbVisionSignatures,
                        cachedEmbMaxVisionBudget
                    )
                    try {
                        return runBatchOnce(cpuEngine, cpuTokens)
                    } catch (cpuErr: Throwable) {
                        lastErr = cpuErr
                        Log.w(TAG, "Multimodal CPU fallback pass failed (tokens=$cpuTokens): ${cpuErr.message}")
                        if (cpuTokens != null) {
                            try {
                                return runBatchOnce(cpuEngine, null)
                            } catch (defErr: Throwable) {
                                lastErr = defErr
                                Log.w(TAG, "Multimodal CPU default-options retry failed: ${defErr.message}")
                            }
                        }
                    }
                }
            } else if (needVision && firstTokens != null) {
                try {
                    return runBatchOnce(firstEngine, null)
                } catch (defErr: Throwable) {
                    lastErr = defErr
                    Log.w(TAG, "Multimodal default-options retry failed: ${defErr.message}")
                }
            }

            val backendLabel = if (cachedEmbVisionIsGpu || cachedEmbMainIsGpu) "GPU" else "CPU"
            if (lastEmbeddingError.isBlank()) {
                lastEmbeddingError = "${embFile.name} ($backendLabel): ${lastErr?.javaClass?.simpleName}: ${lastErr?.message ?: "Unknown error"}"
            }
            Log.w(TAG, "EmbeddingGemma 2 multimodal pass failed completely: $lastEmbeddingError")
            releaseEmbeddingEngineLocked()
            return FloatArray(0)
        }
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
                releaseEmbeddingEngineLocked()
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
        
        // v6.1 Maximum RAM Guard: 0.8GB (Mid-range protection via live ActivityManager memory inside :inference_core)
        val freeRam = getWorkerAvailableRamGB()
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
        if (partial is Message) {
            val sb = StringBuilder()
            for (c in partial.contents.contents) {
                if (c is Content.Text) sb.append(c.text)
            }
            if (sb.isNotEmpty()) return sb.toString()
        }
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

    override fun onDestroy() {
        releaseEmbeddingEngineLocked()
        releaseResources()
        try { shutdownKernelNative() } catch (e: Exception) {}
        super.onDestroy()
    }
}
