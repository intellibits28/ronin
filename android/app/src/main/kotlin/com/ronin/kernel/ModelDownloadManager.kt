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
        val requiresHfToken: Boolean = false
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
     * Curated catalog of LiteRT-LM (`.litertlm`) reasoning and 768D embedding models.
     */
    val CURATED_MODELS: List<DownloadableModelSpec> = listOf(
        DownloadableModelSpec(
            id = "gemma-4-e2b-it-int4",
            displayName = "Gemma 4 E2B Instruct (Sovereign Default)",
            category = ModelCategory.REASONING,
            hfRepo = "litert-community/Gemma-4-E2B-it-litert-lm",
            fileName = "gemma-4-E2B-it.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/Gemma-4-E2B-it-litert-lm/resolve/main/gemma-4-E2B-it.litertlm",
            approxSizeMb = 1520,
            minRamGb = 5.5f,
            recommendedRamGb = 6.0f,
            quantization = "INT4 • 2B Effective",
            description = "Primary Ronin v5.1 brain. Strong Myanmar Unicode fluency, tool-calling, and low-latency edge reasoning.",
            supportsMyanmar = true
        ),
        DownloadableModelSpec(
            id = "embedding-gemma-2-740m-int4",
            displayName = "EmbeddingGemma 2 740M (768D On-Demand RAG)",
            category = ModelCategory.EMBEDDING,
            hfRepo = "litert-community/EmbeddingGemma-2-740M-litert-lm",
            fileName = "embedding-gemma-2-740m-int4.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/EmbeddingGemma-2-740M-litert-lm/resolve/main/embedding-gemma-2-740m-int4.litertlm",
            approxSizeMb = 420,
            minRamGb = 4.0f,
            recommendedRamGb = 6.0f,
            quantization = "INT4 • 768D Vector",
            description = "On-demand 768-dimensional semantic embedding model for `/docsearch`, `/summarize`, and `/research` Hybrid RRF.",
            supportsMyanmar = true
        ),
        DownloadableModelSpec(
            id = "gemma-3-1b-it-int4",
            displayName = "Gemma 3 1B Instruct (Ultra-Light)",
            category = ModelCategory.REASONING,
            hfRepo = "litert-community/Gemma3-1B-IT",
            fileName = "gemma3-1b-it-int4.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/Gemma3-1B-IT/resolve/main/gemma3-1b-it-int4.litertlm",
            approxSizeMb = 680,
            minRamGb = 3.5f,
            recommendedRamGb = 4.0f,
            quantization = "INT4 • 1B",
            description = "Fastest token/s and lowest thermal footprint. Ideal for 4GB–6GB RAM phones or battery-saving mode.",
            supportsMyanmar = true
        ),
        DownloadableModelSpec(
            id = "gemma-4-e4b-it-int4",
            displayName = "Gemma 4 E4B Instruct (Pro Flagship)",
            category = ModelCategory.REASONING,
            hfRepo = "litert-community/Gemma-4-E4B-it-litert-lm",
            fileName = "gemma-4-E4B-it.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/Gemma-4-E4B-it-litert-lm/resolve/main/gemma-4-E4B-it.litertlm",
            approxSizeMb = 2650,
            minRamGb = 7.5f,
            recommendedRamGb = 8.0f,
            quantization = "INT4 • 4B Effective",
            description = "Deepest analytical synthesis for SHM engineering diagnostics and complex Myanmar/English research.",
            supportsMyanmar = true
        ),
        DownloadableModelSpec(
            id = "deepseek-r1-distill-qwen-1.5b",
            displayName = "DeepSeek-R1 Distill Qwen 1.5B",
            category = ModelCategory.REASONING,
            hfRepo = "litert-community/DeepSeek-R1-Distill-Qwen-1.5B",
            fileName = "deepseek-r1-distill-qwen-1.5b-int4.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/DeepSeek-R1-Distill-Qwen-1.5B/resolve/main/deepseek-r1-distill-qwen-1.5b-int4.litertlm",
            approxSizeMb = 1120,
            minRamGb = 4.5f,
            recommendedRamGb = 6.0f,
            quantization = "INT4 • 1.5B CoT",
            description = "Chain-of-Thought ([THINK]) specialist model for math, code, and multi-step logic.",
            supportsMyanmar = false
        ),
        DownloadableModelSpec(
            id = "qwen-2.5-1.5b-instruct",
            displayName = "Qwen 2.5 1.5B Instruct",
            category = ModelCategory.REASONING,
            hfRepo = "litert-community/Qwen2.5-1.5B-Instruct",
            fileName = "qwen2.5-1.5b-instruct-int4.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/Qwen2.5-1.5B-Instruct/resolve/main/qwen2.5-1.5b-instruct-int4.litertlm",
            approxSizeMb = 1100,
            minRamGb = 4.5f,
            recommendedRamGb = 6.0f,
            quantization = "INT4 • 1.5B",
            description = "Compact coding, JSON tool-calling, and structured instruction-following model.",
            supportsMyanmar = false
        ),
        DownloadableModelSpec(
            id = "gecko-multilingual-110m-int8",
            displayName = "Gecko Multilingual 110M (Compact 768D RAG)",
            category = ModelCategory.EMBEDDING,
            hfRepo = "litert-community/Gecko-110m-en-litert",
            fileName = "gecko-multilingual-110m-int8.litertlm",
            downloadUrl = "https://huggingface.co/litert-community/Gecko-110m-en-litert/resolve/main/gecko-multilingual-110m-int8.litertlm",
            approxSizeMb = 118,
            minRamGb = 3.0f,
            recommendedRamGb = 4.0f,
            quantization = "INT8 • 768D Vector",
            description = "Ultra-compact 768D embedding model for instant document indexing on memory-constrained devices.",
            supportsMyanmar = true
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
            } else if (profile.totalRamGb < 5.5f && spec.id == "gecko-multilingual-110m-int8") {
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

    fun cancelDownload(modelId: String) {
        activeCancelFlags[modelId]?.set(true)
    }

    /**
     * Downloads a model from Hugging Face with HTTP Range resume support and atomic `.part` swap.
     */
    suspend fun downloadModel(
        context: Context,
        spec: DownloadableModelSpec,
        hfToken: String = "",
        onProgress: (DownloadProgress) -> Unit
    ): Result<File> = withContext(Dispatchers.IO) {
        val modelsDir = File(context.filesDir, "models").apply { if (!exists()) mkdirs() }
        val finalFile = File(modelsDir, spec.fileName)
        val partFile = File(modelsDir, "${spec.fileName}.part")

        val cancelFlag = AtomicBoolean(false)
        activeCancelFlags[spec.id] = cancelFlag

        try {
            val existingBytes = if (partFile.exists()) partFile.length() else 0L
            val reqBuilder = Request.Builder()
                .url(spec.downloadUrl)
                .header("User-Agent", "RoninKernel/5.1 (Android; LiteRT-LM Model Downloader)")

            if (hfToken.isNotBlank()) {
                reqBuilder.header("Authorization", "Bearer ${hfToken.trim()}")
            }
            if (existingBytes > 0L) {
                reqBuilder.header("Range", "bytes=$existingBytes-")
            }

            downloadHttpClient.newCall(reqBuilder.build()).execute().use { response ->
                val code = response.code
                if (code == 401 || code == 403) {
                    return@withContext Result.failure(
                        IllegalStateException("Hugging Face access denied (HTTP $code). If this repository is gated, add your HF Read Token in Settings.")
                    )
                }
                if (code !in 200..299 && code != 416) {
                    return@withContext Result.failure(
                        IllegalStateException("Download failed with HTTP $code from Hugging Face.")
                    )
                }

                val isResumed = code == 206 && existingBytes > 0L
                val body = response.body ?: return@withContext Result.failure(IllegalStateException("Empty response body"))
                val contentLength = body.contentLength()
                val totalBytes = if (contentLength > 0) {
                    if (isResumed) existingBytes + contentLength else contentLength
                } else {
                    spec.approxSizeMb.toLong() * 1024L * 1024L
                }

                RandomAccessFile(partFile, "rw").use { raf ->
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
                                return@withContext Result.failure(IllegalStateException("Download paused/cancelled by user (resume supported)."))
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
                                        fileName = spec.fileName,
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

                if (partFile.length() < 1024L) {
                    partFile.delete()
                    return@withContext Result.failure(IllegalStateException("Downloaded file is too small or corrupted."))
                }

                if (finalFile.exists()) finalFile.delete()
                if (!partFile.renameTo(finalFile)) {
                    partFile.copyTo(finalFile, overwrite = true)
                    partFile.delete()
                }

                onProgress(
                    DownloadProgress(
                        modelId = spec.id,
                        fileName = spec.fileName,
                        downloadedBytes = finalFile.length(),
                        totalBytes = finalFile.length(),
                        progressPercent = 100,
                        speedMbPerSec = 0f,
                        isCompleted = true
                    )
                )
                Log.i(TAG, "Successfully downloaded ${finalFile.name} (${finalFile.length()} bytes)")
                Result.success(finalFile)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Model download error for ${spec.fileName}: ${e.message}")
            Result.failure(e)
        } finally {
            activeCancelFlags.remove(spec.id)
        }
    }
}
