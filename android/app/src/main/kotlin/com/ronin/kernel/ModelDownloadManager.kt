package com.ronin.kernel

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.File
import java.io.RandomAccessFile
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Ronin v5.1 — Hardware-Aware Hugging Face Model Hub & Resumable Downloader.
 *
 * Inspects device CPU ABI, core count, and total/available RAM to recommend compatible
 * Gemma 4, Gemma 3, Qwen/DeepSeek, and EmbeddingGemma 2 (768D) models for on-device execution.
 */
object ModelDownloadManager {
    private const val TAG = "RoninModelHub"

    enum class ModelCategory(val label: String) {
        REASONING("REASONING BRAIN (LLM)"),
        EMBEDDING("SEMANTIC RAG EMBEDDING (768D)")
    }

    enum class CompatibilityTier(val badgeText: String) {
        RECOMMENDED("★ RECOMMENDED FOR THIS DEVICE"),
        ULTRA_LIGHT("⚡ ULTRA-FAST / LOW RAM"),
        COMPATIBLE("✓ COMPATIBLE"),
        HEAVY_RAM_WARNING("⚠ REQUIRES HIGH RAM")
    }

    data class DeviceHardwareProfile(
        val totalRamGb: Float,
        val availRamGb: Float,
        val cpuCores: Int,
        val primaryAbi: String,
        val socModel: String
    ) {
        val isArm64: Boolean
            get() = primaryAbi.contains("arm64", ignoreCase = true) || primaryAbi.contains("aarch64", ignoreCase = true)
    }

    data class DownloadableModelSpec(
        val id: String,
        val displayName: String,
        val category: ModelCategory,
        val hfRepo: String,
        val fileName: String,
        val downloadUrl: String,
        val approxSizeMb: Int,
        val minRamGb: Float,
        val recommendedRamGb: Float,
        val quantization: String,
        val description: String,
        val supportsMyanmar: Boolean = true,
        val requiresHfToken: Boolean = false,
        val fallbackUrls: List<String> = emptyList()
    )

    data class DownloadProgress(
        val modelId: String,
        val fileName: String,
        val downloadedBytes: Long,
        val totalBytes: Long,
        val progressPercent: Int,
        val speedMbPerSec: Float,
        val isCompleted: Boolean = false,
        val error: String? = null
    )

    private val activeCancelFlags = ConcurrentHashMap<String, AtomicBoolean>()

    private val downloadHttpClient: OkHttpClient by lazy {
        OkHttpClient.Builder()
            .connectTimeout(20, TimeUnit.SECONDS)
            .readTimeout(60, TimeUnit.SECONDS)
            .followRedirects(true)
            .followSslRedirects(true)
            .build()
    }

    /**
     * Curated catalog of verified ungated LiteRT-LM (`.litertlm`) reasoning and 768D embedding models.
     * Every URL in this catalog is verified against Hugging Face API (`gated: False`, HTTP 200 OK).
     */
    val CURATED_MODELS: List<DownloadableModelSpec> = listOf(
        DownloadableModelSpec(
            id = "gemma-4-e2b-it-int4",
            displayName = "Gemma 4 E2B Instruct (Sovereign Default)",
            category = ModelCategory.REASONING,
            hfRepo = "litert-community/gemma-4-E2B-it-litert-lm",
            fileName = "gemma-4-E2B-it.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/gemma-4-E2B-it-litert-lm/resolve/main/gemma-4-E2B-it.litertlm?download=true",
            approxSizeMb = 2468,
            minRamGb = 5.5f,
            recommendedRamGb = 6.0f,
            quantization = "INT4 • 2B Effective",
            description = "Primary Ronin v5.1 brain (Ungated). Strong Myanmar Unicode fluency, tool-calling, and low-latency edge reasoning.",
            supportsMyanmar = true,
            requiresHfToken = false,
            fallbackUrls = listOf(
                "https://huggingface.co/nqd145/Gemma-4-E2B-it-abliterated-litertlm/resolve/main/Gemma-4-E2B-it-abliterated.litertlm?download=true"
            )
        ),
        DownloadableModelSpec(
            id = "embedding-gemma-2-740m-int4",
            displayName = "EmbeddingGemma 2 740M (768D On-Demand RAG)",
            category = ModelCategory.EMBEDDING,
            hfRepo = "litert-community/embeddinggemma-2-740m-litert-lm",
            fileName = "embeddinggemma-2-740m.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/embeddinggemma-2-740m-litert-lm/resolve/main/embeddinggemma-2-740m.litertlm?download=true",
            approxSizeMb = 462,
            minRamGb = 4.0f,
            recommendedRamGb = 6.0f,
            quantization = "INT4 • 768D Vector",
            description = "Official ungated 768D semantic embedding model for `/docsearch`, `/summarize`, and `/research` Hybrid RRF.",
            supportsMyanmar = true,
            requiresHfToken = false
        ),
        DownloadableModelSpec(
            id = "gemma-3-1b-it-int4",
            displayName = "Gemma 3 1B Instruct (Ultra-Light)",
            category = ModelCategory.REASONING,
            hfRepo = "na5h13/Gemma3-1B-IT-litert-lm",
            fileName = "gemma3-1b-it-int4.litertlm",
            downloadUrl = "https://huggingface.co/na5h13/Gemma3-1B-IT-litert-lm/resolve/main/gemma3-1b-it-int4.litertlm?download=true",
            approxSizeMb = 557,
            minRamGb = 3.5f,
            recommendedRamGb = 4.0f,
            quantization = "INT4 • 1B",
            description = "Fastest token/s and lowest thermal footprint (Ungated mirror, no token needed). Ideal for 4GB–6GB RAM devices.",
            supportsMyanmar = true,
            requiresHfToken = false,
            fallbackUrls = listOf(
                "https://huggingface.co/litert-community/Gemma3-1B-IT/resolve/main/gemma3-1b-it-int4.litertlm?download=true"
            )
        ),
        DownloadableModelSpec(
            id = "gemma-4-e4b-it-int4",
            displayName = "Gemma 4 E4B Instruct (Pro Flagship)",
            category = ModelCategory.REASONING,
            hfRepo = "litert-community/gemma-4-E4B-it-litert-lm",
            fileName = "gemma-4-E4B-it.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/gemma-4-E4B-it-litert-lm/resolve/main/gemma-4-E4B-it.litertlm?download=true",
            approxSizeMb = 3490,
            minRamGb = 7.5f,
            recommendedRamGb = 8.0f,
            quantization = "INT4 • 4B Effective",
            description = "Deepest analytical synthesis (Ungated) for SHM engineering diagnostics and complex Myanmar/English research.",
            supportsMyanmar = true,
            requiresHfToken = false
        ),
        DownloadableModelSpec(
            id = "qwen-3.5-0.8b-int8",
            displayName = "Qwen 3.5 0.8B Instruct (Fast Edge)",
            category = ModelCategory.REASONING,
            hfRepo = "litert-community/Qwen3.5-0.8B",
            fileName = "Qwen3.5-0.8B_int8.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/Qwen3.5-0.8B/resolve/main/Qwen3.5-0.8B_int8.litertlm?download=true",
            approxSizeMb = 920,
            minRamGb = 4.0f,
            recommendedRamGb = 6.0f,
            quantization = "INT8 • 0.8B",
            description = "Official ungated Qwen 3.5 0.8B LiteRT-LM model for ultra-fast coding, math, and JSON tool-calling.",
            supportsMyanmar = false,
            requiresHfToken = false
        ),
        DownloadableModelSpec(
            id = "qwen-3.5-2b-int8",
            displayName = "Qwen 3.5 2B Instruct",
            category = ModelCategory.REASONING,
            hfRepo = "litert-community/Qwen3.5-2B",
            fileName = "Qwen3.5-2B_int8.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/Qwen3.5-2B/resolve/main/Qwen3.5-2B_int8.litertlm?download=true",
            approxSizeMb = 2021,
            minRamGb = 5.5f,
            recommendedRamGb = 6.0f,
            quantization = "INT8 • 2B",
            description = "Official ungated Qwen 3.5 2B LiteRT-LM model for structured reasoning, coding, and agentic workflows.",
            supportsMyanmar = false,
            requiresHfToken = false
        ),
        DownloadableModelSpec(
            id = "embedding-gemma-2-270m-int8",
            displayName = "EmbeddingGemma 2 Text 270M (Ultra-Light 768D RAG)",
            category = ModelCategory.EMBEDDING,
            hfRepo = "litert-community/embeddinggemma-2-text-270m-litert-lm",
            fileName = "embeddinggemma-2-text-270m.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/embeddinggemma-2-text-270m-litert-lm/resolve/main/embeddinggemma-2-text-270m.litertlm?download=true",
            approxSizeMb = 157,
            minRamGb = 3.0f,
            recommendedRamGb = 4.0f,
            quantization = "INT8 • 768D Vector",
            description = "Ultra-compact 157 MB EmbeddingGemma 2 model (Ungated) for instant document indexing on 4GB–6GB RAM devices.",
            supportsMyanmar = true,
            requiresHfToken = false
        )
    )

    /**
     * Detects the live device RAM, CPU cores, and ABI profile.
     */
    fun detectHardwareProfile(context: Context): DeviceHardwareProfile {
        return try {
            val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            val mi = ActivityManager.MemoryInfo()
            am.getMemoryInfo(mi)
            val totalGb = mi.totalMem / (1024f * 1024f * 1024f)
            val availGb = mi.availMem / (1024f * 1024f * 1024f)
            val cores = Runtime.getRuntime().availableProcessors()
            val abi = Build.SUPPORTED_ABIS.firstOrNull() ?: "arm64-v8a"
            val soc = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                Build.SOC_MODEL.takeIf { it.isNotBlank() && it != Build.UNKNOWN } ?: Build.HARDWARE
            } else {
                Build.HARDWARE ?: "ARM64"
            }
            DeviceHardwareProfile(
                totalRamGb = totalGb,
                availRamGb = availGb,
                cpuCores = cores,
                primaryAbi = abi,
                socModel = soc
            )
        } catch (_: Exception) {
            DeviceHardwareProfile(
                totalRamGb = 8.0f,
                availRamGb = 4.0f,
                cpuCores = 8,
                primaryAbi = "arm64-v8a",
                socModel = "ARM64"
            )
        }
    }

    /**
     * Evaluates compatibility of a model spec against the device's RAM & CPU profile.
     */
    fun assessCompatibility(
        spec: DownloadableModelSpec,
        profile: DeviceHardwareProfile
    ): CompatibilityTier {
        if (!profile.isArm64 || profile.totalRamGb < spec.minRamGb) {
            return CompatibilityTier.HEAVY_RAM_WARNING
        }
        // Highlight primary recommended reasoning & embedding models for the user's RAM tier
        if (spec.category == ModelCategory.EMBEDDING) {
            return if (profile.totalRamGb >= 5.5f && spec.id == "embedding-gemma-2-740m-int4") {
                CompatibilityTier.RECOMMENDED
            } else if (profile.totalRamGb < 5.5f && spec.id == "embedding-gemma-2-270m-int8") {
                CompatibilityTier.RECOMMENDED
            } else {
                CompatibilityTier.COMPATIBLE
            }
        }

        // Reasoning models
        return when {
            profile.totalRamGb >= 7.8f && (spec.id == "gemma-4-e2b-it-int4" || spec.id == "gemma-4-e4b-it-int4") ->
                CompatibilityTier.RECOMMENDED
            profile.totalRamGb in 5.5f..7.8f && spec.id == "gemma-4-e2b-it-int4" ->
                CompatibilityTier.RECOMMENDED
            profile.totalRamGb < 5.5f && spec.id == "gemma-3-1b-it-int4" ->
                CompatibilityTier.RECOMMENDED
            spec.approxSizeMb <= 750 ->
                CompatibilityTier.ULTRA_LIGHT
            else ->
                CompatibilityTier.COMPATIBLE
        }
    }

    /**
     * Returns the catalog sorted with RECOMMENDED models for this device first.
     */
    fun getCatalogForDevice(profile: DeviceHardwareProfile): List<Pair<DownloadableModelSpec, CompatibilityTier>> {
        return CURATED_MODELS.map { spec ->
            spec to assessCompatibility(spec, profile)
        }.sortedWith(
            compareBy<Pair<DownloadableModelSpec, CompatibilityTier>> {
                when (it.second) {
                    CompatibilityTier.RECOMMENDED -> 0
                    CompatibilityTier.ULTRA_LIGHT -> 1
                    CompatibilityTier.COMPATIBLE -> 2
                    CompatibilityTier.HEAVY_RAM_WARNING -> 3
                }
            }.thenBy { it.first.category.ordinal }
        )
    }

    /**
     * Builds a custom DownloadableModelSpec from a Hugging Face `owner/repo` + `filename` or direct HTTPS URL.
     */
    fun buildCustomHfSpec(repoOrUrl: String, customFileName: String = ""): DownloadableModelSpec? {
        val trimmed = repoOrUrl.trim()
        if (trimmed.isEmpty()) return null

        val url: String
        val resolvedName: String
        val repoLabel: String

        if (trimmed.startsWith("https://", ignoreCase = true)) {
            url = trimmed
            resolvedName = customFileName.ifBlank {
                trimmed.substringAfterLast('/').substringBefore('?').ifBlank { "custom_model.litertlm" }
            }
            repoLabel = trimmed.removePrefix("https://huggingface.co/").substringBefore("/resolve/")
        } else {
            // Format: owner/repo/filename.litertlm OR owner/repo + customFileName
            val segments = trimmed.split("/").filter { it.isNotBlank() }
            if (segments.size < 2) return null
            repoLabel = "${segments[0]}/${segments[1]}"
            resolvedName = if (customFileName.isNotBlank()) {
                customFileName.trim()
            } else if (segments.size >= 3) {
                segments.drop(2).joinToString("/")
            } else {
                return null
            }
            url = "https://huggingface.co/$repoLabel/resolve/main/$resolvedName"
        }

        val safeFileName = resolvedName.substringAfterLast('/').replace(Regex("[^a-zA-Z0-9._-]"), "_")
        if (!safeFileName.endsWith(".litertlm") && !safeFileName.endsWith(".bin") && !safeFileName.endsWith(".task")) {
            return null
        }

        val isEmb = safeFileName.contains("embed", ignoreCase = true) || safeFileName.contains("gecko", ignoreCase = true)
        return DownloadableModelSpec(
            id = "custom_${safeFileName.lowercase()}",
            displayName = safeFileName,
            category = if (isEmb) ModelCategory.EMBEDDING else ModelCategory.REASONING,
            hfRepo = repoLabel,
            fileName = safeFileName,
            downloadUrl = url,
            approxSizeMb = 1000,
            minRamGb = 4.0f,
            recommendedRamGb = 6.0f,
            quantization = "Custom HF",
            description = "Custom Hugging Face model from $repoLabel"
        )
    }

    /**
     * Queries the Hugging Face Hub API (`https://huggingface.co/api/models/<owner>/<repo>`)
     * to auto-discover the best `.litertlm` file in a repository when only `owner/repo` is given
     * or if a filename changed upstream.
     */
    fun discoverLitertFileFromHfApi(hfRepo: String, hfToken: String = ""): Pair<String, String>? {
        return try {
            val apiUrl = "https://huggingface.co/api/models/${hfRepo.trim().removePrefix("/")}"
            val reqBuilder = Request.Builder()
                .url(apiUrl)
                .header("User-Agent", "RoninKernel/5.1 (Android; LiteRT-LM Model Downloader)")
            if (hfToken.isNotBlank()) {
                reqBuilder.header("Authorization", "Bearer ${hfToken.trim()}")
            }
            downloadHttpClient.newCall(reqBuilder.build()).execute().use { resp ->
                if (!resp.isSuccessful) return null
                val bodyStr = resp.body?.string() ?: return null
                val json = org.json.JSONObject(bodyStr)
                val siblings = json.optJSONArray("siblings") ?: return null
                val candidates = mutableListOf<String>()
                for (i in 0 until siblings.length()) {
                    val rfn = siblings.optJSONObject(i)?.optString("rfilename") ?: continue
                    if (rfn.endsWith(".litertlm", ignoreCase = true)) {
                        candidates.add(rfn)
                    }
                }
                if (candidates.isEmpty()) return null
                // Prefer general CPU/GPU .litertlm files over SoC-specific NPU/Web variants
                val chosen = candidates.firstOrNull {
                    !it.contains("_Qualcomm_", true) &&
                        !it.contains("_MediaTek_", true) &&
                        !it.contains("_Google_Tensor_", true) &&
                        !it.contains("_Intel_", true) &&
                        !it.contains("-web.", true) &&
                        !it.contains("_NPU_", true)
                } ?: candidates.first()
                chosen to "https://huggingface.co/$hfRepo/resolve/main/$chosen?download=true"
            }
        } catch (e: Exception) {
            Log.w(TAG, "HF API discovery failed for $hfRepo: ${e.message}")
            null
        }
    }

    fun cancelDownload(modelId: String) {
        activeCancelFlags[modelId]?.set(true)
    }

    /**
     * Downloads a model from Hugging Face with HTTP Range resume support, fallback mirror retry,
     * HF API filename auto-discovery, and atomic `.part` swap.
     */
    suspend fun downloadModel(
        context: Context,
        spec: DownloadableModelSpec,
        hfToken: String = "",
        onProgress: (DownloadProgress) -> Unit
    ): Result<File> = withContext(Dispatchers.IO) {
        val modelsDir = File(context.filesDir, "models").apply { if (!exists()) mkdirs() }
        var resolvedFileName = spec.fileName
        val cancelFlag = AtomicBoolean(false)
        activeCancelFlags[spec.id] = cancelFlag

        // Emit immediate initial progress so UI shows active state right away
        onProgress(
            DownloadProgress(
                modelId = spec.id,
                fileName = resolvedFileName,
                downloadedBytes = 0L,
                totalBytes = spec.approxSizeMb.toLong() * 1024L * 1024L,
                progressPercent = 0,
                speedMbPerSec = 0f
            )
        )

        try {
            val candidateUrls = mutableListOf<String>()
            candidateUrls.add(spec.downloadUrl)
            candidateUrls.addAll(spec.fallbackUrls)

            var lastErrorMsg = "Download failed from Hugging Face."

            for (attemptUrl in candidateUrls) {
                var currentUrl = attemptUrl
                val partFile = File(modelsDir, "$resolvedFileName.part")
                val finalFile = File(modelsDir, resolvedFileName)
                var existingBytes = if (partFile.exists()) partFile.length() else 0L

                fun buildRequest(url: String, rangeStart: Long): Request {
                    val rb = Request.Builder()
                        .url(url)
                        .header("User-Agent", "Mozilla/5.0 (Linux; Android 14; RoninKernel/5.1) AppleWebKit/537.36")
                    if (hfToken.isNotBlank()) {
                        rb.header("Authorization", "Bearer ${hfToken.trim()}")
                    }
                    if (rangeStart > 0L) {
                        rb.header("Range", "bytes=$rangeStart-")
                    }
                    return rb.build()
                }

                var response = downloadHttpClient.newCall(buildRequest(currentUrl, existingBytes)).execute()

                // If Range request returned 416 or 400, reset .part file and retry from 0
                if ((response.code == 416 || response.code == 400) && existingBytes > 0L) {
                    response.close()
                    partFile.delete()
                    existingBytes = 0L
                    response = downloadHttpClient.newCall(buildRequest(currentUrl, 0L)).execute()
                }

                // If 404 on direct URL, auto-query Hugging Face Hub API for the exact .litertlm filename
                if (response.code == 404 && spec.hfRepo.contains("/")) {
                    response.close()
                    val discovered = discoverLitertFileFromHfApi(spec.hfRepo, hfToken)
                    if (discovered != null && discovered.second != currentUrl) {
                        resolvedFileName = discovered.first.substringAfterLast('/')
                        currentUrl = discovered.second
                        existingBytes = 0L
                        response = downloadHttpClient.newCall(buildRequest(currentUrl, 0L)).execute()
                    }
                }

                response.use { resp ->
                    val code = resp.code
                    if (code == 401 || code == 403) {
                        lastErrorMsg = "Hugging Face access denied (HTTP $code) for ${spec.hfRepo}. If this repo is gated, enter your HF Read Token (hf_...) under Custom Hugging Face Repo / Token."
                        return@use
                    }
                    if (code !in 200..299) {
                        lastErrorMsg = "Hugging Face returned HTTP $code for ${spec.hfRepo}."
                        return@use
                    }

                    val targetPartFile = File(modelsDir, "$resolvedFileName.part")
                    val targetFinalFile = File(modelsDir, resolvedFileName)
                    val isResumed = code == 206 && existingBytes > 0L
                    val body = resp.body ?: run {
                        lastErrorMsg = "Empty response body from Hugging Face."
                        return@use
                    }
                    val contentLength = body.contentLength()
                    val totalBytes = if (contentLength > 0) {
                        if (isResumed) existingBytes + contentLength else contentLength
                    } else {
                        spec.approxSizeMb.toLong() * 1024L * 1024L
                    }

                    RandomAccessFile(targetPartFile, "rw").use { raf ->
                        if (isResumed) {
                            raf.seek(existingBytes)
                        } else {
                            raf.setLength(0)
                        }

                        val buffer = ByteArray(64 * 1024)
                        var downloaded = if (isResumed) existingBytes else 0L
                        val startTimeMs = System.currentTimeMillis()
                        var lastEmitMs = 0L

                        body.byteStream().use { input ->
                            var read: Int
                            while (input.read(buffer).also { read = it } != -1) {
                                if (cancelFlag.get()) {
                                    val cancelErr = "Download paused/cancelled (tap Download to resume)."
                                    onProgress(
                                        DownloadProgress(
                                            modelId = spec.id,
                                            fileName = resolvedFileName,
                                            downloadedBytes = downloaded,
                                            totalBytes = totalBytes,
                                            progressPercent = if (totalBytes > 0) ((downloaded * 100L) / totalBytes).toInt() else 0,
                                            speedMbPerSec = 0f,
                                            error = cancelErr
                                        )
                                    )
                                    return@withContext Result.failure(IllegalStateException(cancelErr))
                                }
                                raf.write(buffer, 0, read)
                                downloaded += read

                                val now = System.currentTimeMillis()
                                if (now - lastEmitMs >= 250L) {
                                    lastEmitMs = now
                                    val elapsedSec = ((now - startTimeMs) / 1000f).coerceAtLeast(0.1f)
                                    val sessionBytes = downloaded - (if (isResumed) existingBytes else 0L)
                                    val speedMb = (sessionBytes / (1024f * 1024f)) / elapsedSec
                                    val pct = if (totalBytes > 0) ((downloaded * 100L) / totalBytes).toInt().coerceIn(0, 99) else 0
                                    onProgress(
                                        DownloadProgress(
                                            modelId = spec.id,
                                            fileName = resolvedFileName,
                                            downloadedBytes = downloaded,
                                            totalBytes = totalBytes,
                                            progressPercent = pct,
                                            speedMbPerSec = speedMb
                                        )
                                    )
                                }
                            }
                        }
                    }

                    if (targetPartFile.length() < 1024L) {
                        targetPartFile.delete()
                        lastErrorMsg = "Downloaded file is too small or corrupted."
                        return@use
                    }

                    if (targetFinalFile.exists()) targetFinalFile.delete()
                    if (!targetPartFile.renameTo(targetFinalFile)) {
                        targetPartFile.copyTo(targetFinalFile, overwrite = true)
                        targetPartFile.delete()
                    }

                    onProgress(
                        DownloadProgress(
                            modelId = spec.id,
                            fileName = resolvedFileName,
                            downloadedBytes = targetFinalFile.length(),
                            totalBytes = targetFinalFile.length(),
                            progressPercent = 100,
                            speedMbPerSec = 0f,
                            isCompleted = true
                        )
                    )
                    Log.i(TAG, "Successfully downloaded ${targetFinalFile.name} (${targetFinalFile.length()} bytes)")
                    return@withContext Result.success(targetFinalFile)
                }
            }

            onProgress(
                DownloadProgress(
                    modelId = spec.id,
                    fileName = resolvedFileName,
                    downloadedBytes = 0L,
                    totalBytes = 0L,
                    progressPercent = 0,
                    speedMbPerSec = 0f,
                    error = lastErrorMsg
                )
            )
            Result.failure(IllegalStateException(lastErrorMsg))
        } catch (e: Exception) {
            Log.e(TAG, "Model download error for ${spec.fileName}: ${e.message}")
            onProgress(
                DownloadProgress(
                    modelId = spec.id,
                    fileName = resolvedFileName,
                    downloadedBytes = 0L,
                    totalBytes = 0L,
                    progressPercent = 0,
                    speedMbPerSec = 0f,
                    error = e.message ?: "Network error"
                )
            )
            Result.failure(e)
        } finally {
            activeCancelFlags.remove(spec.id)
        }
    }
}
