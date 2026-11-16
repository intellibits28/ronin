package com.ronin.kernel

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.pdf.PdfRenderer
import android.os.ParcelFileDescriptor
import android.util.Log
import com.googlecode.tesseract.android.TessBaseAPI
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.util.concurrent.TimeUnit

/**
 * Phase 3: On-Device OCR Engine for Ronin Kernel.
 * Supports Myanmar Script (mya) and English (eng) using Tesseract 5.x LSTM engine.
 */
object OcrEngine {
    private const val TAG = "Ronin_OcrEngine"
    private const val TESSDATA_DIR = "tessdata"
    private const val DEFAULT_LANG = "mya+eng"
    const val GITHUB_TESSDATA_FAST_URL = "https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main"
    const val GITHUB_TESSDATA_BEST_URL = "https://raw.githubusercontent.com/tesseract-ocr/tessdata_best/main"

    enum class TessdataVariant(val label: String, val baseUrl: String) {
        FAST("tessdata_fast (Fast Int8 LSTM)", GITHUB_TESSDATA_FAST_URL),
        BEST("tessdata_best (High-Accuracy Float LSTM)", GITHUB_TESSDATA_BEST_URL)
    }

    data class InstalledTessModel(
        val langCode: String,
        val fileName: String,
        val sizeBytes: Long,
        val lastModifiedMs: Long
    )

    private val httpClient by lazy {
        OkHttpClient.Builder()
            .connectTimeout(30, TimeUnit.SECONDS)
            .readTimeout(60, TimeUnit.SECONDS)
            .build()
    }

    data class OcrResult(
        val success: Boolean,
        val text: String = "",
        val confidence: Int = 0,
        val language: String = "",
        val processingTimeMs: Long = 0,
        val pageCount: Int = 1,
        val error: String? = null,
        val darkModeInverted: Boolean = false,
        val psmMode: String = "AUTO"
    )

    /**
     * Checks whether the required traineddata files are available on disk.
     */
    fun isLanguageAvailable(context: Context, langCode: String): Boolean {
        val tessDir = File(context.filesDir, TESSDATA_DIR)
        val targetFile = File(tessDir, "$langCode.traineddata")
        return targetFile.exists() && targetFile.length() > 100_000
    }

    /**
     * Lists all installed `.traineddata` language models in `filesDir/tessdata`.
     */
    fun listInstalledModels(context: Context): List<InstalledTessModel> {
        val tessDir = File(context.filesDir, TESSDATA_DIR)
        if (!tessDir.exists()) return emptyList()
        return tessDir.listFiles { f -> f.isFile && f.name.endsWith(".traineddata", ignoreCase = true) }
            ?.map { f ->
                InstalledTessModel(
                    langCode = f.name.substringBeforeLast('.'),
                    fileName = f.name,
                    sizeBytes = f.length(),
                    lastModifiedMs = f.lastModified()
                )
            }
            ?.sortedBy { it.langCode }
            ?: emptyList()
    }

    /**
     * Validates that a file is a genuine non-empty `.traineddata` binary (> 50 KB, valid lang code).
     */
    internal fun validateTrainedDataFile(file: File, targetLangCode: String? = null): Result<String> {
        if (!file.exists() || !file.isFile || !file.canRead()) {
            return Result.failure(IllegalArgumentException("Traineddata file does not exist or is not readable: ${file.path}"))
        }
        val rawLang = (targetLangCode?.takeIf { it.isNotBlank() } ?: file.name.substringBeforeLast('.'))
            .lowercase()
            .trim()
        if (!rawLang.matches(Regex("^[a-z0-9_]{2,16}$"))) {
            return Result.failure(IllegalArgumentException("Invalid language code '$rawLang'. Expected format like 'mya', 'eng', or 'mya_custom'."))
        }
        if (file.length() < 50_000L) {
            return Result.failure(IllegalArgumentException("File is too small (${file.length()} bytes) to be a valid Tesseract .traineddata model."))
        }
        return Result.success(rawLang)
    }

    /**
     * Imports a custom `.traineddata` file from local storage into `filesDir/tessdata/<lang>.traineddata`.
     */
    suspend fun importCustomTrainedData(
        context: Context,
        sourceFile: File,
        customLangCode: String? = null
    ): Result<InstalledTessModel> = withContext(Dispatchers.IO) {
        val validLangRes = validateTrainedDataFile(sourceFile, customLangCode)
        val langCode = validLangRes.getOrElse { return@withContext Result.failure(it) }

        val tessDir = File(context.filesDir, TESSDATA_DIR).apply { if (!exists()) mkdirs() }
        val destFile = File(tessDir, "$langCode.traineddata")
        val tempFile = File(tessDir, "$langCode.traineddata.import_tmp")

        try {
            sourceFile.inputStream().use { input ->
                FileOutputStream(tempFile).use { output ->
                    input.copyTo(output)
                }
            }
            if (destFile.exists()) destFile.delete()
            if (!tempFile.renameTo(destFile)) {
                tempFile.copyTo(destFile, overwrite = true)
                tempFile.delete()
            }
            Result.success(
                InstalledTessModel(
                    langCode = langCode,
                    fileName = destFile.name,
                    sizeBytes = destFile.length(),
                    lastModifiedMs = destFile.lastModified()
                )
            )
        } catch (e: Exception) {
            tempFile.delete()
            Result.failure(e)
        }
    }

    /**
     * Force-updates `.traineddata` language models from `tessdata_fast` or `tessdata_best`.
     */
    suspend fun updateLanguageModels(
        context: Context,
        languages: List<String> = listOf("mya", "eng"),
        variant: TessdataVariant = TessdataVariant.FAST,
        onProgress: ((String) -> Unit)? = null
    ): Result<List<InstalledTessModel>> = withContext(Dispatchers.IO) {
        val tessDir = File(context.filesDir, TESSDATA_DIR).apply { if (!exists()) mkdirs() }
        val updated = mutableListOf<InstalledTessModel>()

        for (rawLang in languages) {
            val lang = rawLang.trim().lowercase()
            if (!lang.matches(Regex("^[a-z0-9_]{2,16}$"))) continue
            val destFile = File(tessDir, "$lang.traineddata")
            val url = "${variant.baseUrl}/$lang.traineddata"
            onProgress?.invoke("Downloading $lang.traineddata (${variant.label})...")
            val ok = downloadFile(url, destFile) { p ->
                onProgress?.invoke("$lang.traineddata — $p")
            }
            if (!ok) {
                return@withContext Result.failure(
                    IllegalStateException("Failed to download $lang.traineddata from ${variant.label}")
                )
            }
            updated.add(
                InstalledTessModel(
                    langCode = lang,
                    fileName = destFile.name,
                    sizeBytes = destFile.length(),
                    lastModifiedMs = destFile.lastModified()
                )
            )
        }
        Result.success(updated)
    }

    /**
     * Ensures traineddata for the given language is installed in filesDir/tessdata.
     * Looks first in Android assets, then falls back to downloading if network is available.
     */
    suspend fun ensureLanguageData(
        context: Context,
        langCode: String,
        onProgress: ((String) -> Unit)? = null
    ): Boolean = withContext(Dispatchers.IO) {
        val tessDir = File(context.filesDir, TESSDATA_DIR)
        if (!tessDir.exists()) {
            tessDir.mkdirs()
        }

        val targetFile = File(tessDir, "$langCode.traineddata")
        if (targetFile.exists() && targetFile.length() > 100_000) {
            return@withContext true
        }

        // 1. Try copying from assets
        val assetCopied = copyFromAssets(context, "$TESSDATA_DIR/$langCode.traineddata", targetFile)
        if (assetCopied && targetFile.length() > 100_000) {
            Log.i(TAG, "Successfully loaded $langCode.traineddata from assets.")
            return@withContext true
        }

        // 2. Download from official tessdata_fast repository
        onProgress?.invoke("Downloading $langCode language model (~4 MB)...")
        val downloadUrl = "$GITHUB_TESSDATA_FAST_URL/$langCode.traineddata"
        Log.i(TAG, "Downloading traineddata from: $downloadUrl")

        return@withContext downloadFile(downloadUrl, targetFile, onProgress)
    }

    private fun copyFromAssets(context: Context, assetPath: String, destFile: File): Boolean {
        return try {
            val assetManager = context.assets
            assetManager.open(assetPath).use { input ->
                FileOutputStream(destFile).use { output ->
                    input.copyTo(output)
                }
            }
            true
        } catch (_: Exception) {
            false
        }
    }

    private fun downloadFile(url: String, destFile: File, onProgress: ((String) -> Unit)?): Boolean {
        return try {
            val request = Request.Builder().url(url).build()
            val response = httpClient.newCall(request).execute()
            if (!response.isSuccessful) {
                Log.e(TAG, "Download failed with HTTP ${response.code}")
                return false
            }

            val body = response.body ?: return false
            val contentLength = body.contentLength()

            val tempFile = File(destFile.parentFile, "${destFile.name}.tmp")
            body.byteStream().use { input ->
                FileOutputStream(tempFile).use { output ->
                    val buffer = ByteArray(8192)
                    var totalBytes = 0L
                    var bytesRead: Int
                    while (input.read(buffer).also { bytesRead = it } != -1) {
                        output.write(buffer, 0, bytesRead)
                        totalBytes += bytesRead
                        if (contentLength > 0 && totalBytes % (256 * 1024) == 0L) {
                            val pct = (totalBytes * 100 / contentLength).toInt()
                            onProgress?.invoke("Downloading: $pct%")
                        }
                    }
                }
            }

            if (tempFile.exists() && tempFile.length() > 100_000) {
                if (destFile.exists()) destFile.delete()
                tempFile.renameTo(destFile)
                Log.i(TAG, "Download complete: ${destFile.name} (${destFile.length()} bytes)")
                true
            } else {
                tempFile.delete()
                false
            }
        } catch (e: Exception) {
            Log.e(TAG, "Exception during traineddata download: ${e.message}", e)
            false
        }
    }

    /**
     * Performs optical character recognition on an image file or PDF page.
     */
    suspend fun recognizeFile(
        context: Context,
        file: File,
        language: String = DEFAULT_LANG,
        onStatus: ((String) -> Unit)? = null
    ): OcrResult = withContext(Dispatchers.IO) {
        val startTime = System.currentTimeMillis()

        if (!file.exists() || !file.canRead()) {
            return@withContext OcrResult(success = false, error = "File cannot be read: ${file.path}")
        }

        // Ensure language models are ready
        val langs = language.split("+").map { it.trim() }.filter { it.isNotEmpty() }
        for (lang in langs) {
            val ready = ensureLanguageData(context, lang) { status ->
                onStatus?.invoke(status)
            }
            if (!ready) {
                return@withContext OcrResult(
                    success = false,
                    error = "Could not initialize OCR model for language: '$lang'. Check internet connection for initial model download."
                )
            }
        }

        onStatus?.invoke("Processing image and recognizing text...")

        val lowerName = file.name.lowercase()
        return@withContext if (lowerName.endsWith(".pdf")) {
            recognizePdfPages(context, file, language, startTime, maxPages = 8, onStatus = onStatus)
        } else {
            recognizeImageFile(context, file, language, startTime)
        }
    }

    internal fun isMeaningfulOcrLine(line: String): Boolean {
        val trimmed = line.trim()
        if (trimmed.length < 2) return false
        val validChars = trimmed.count { it.isLetterOrDigit() || it in '\u1000'..'\u109F' || it == '+' }
        return validChars >= 2
    }

    internal fun mergeUniqueOcrLines(primaryText: String, secondaryText: String): String {
        val primaryLines = primaryText.lines().map { it.trim() }.filter { it.isNotBlank() }
        val secondaryLines = secondaryText.lines().map { it.trim() }.filter { isMeaningfulOcrLine(it) }
        if (primaryLines.isEmpty()) return secondaryLines.joinToString("\n")
        if (secondaryLines.isEmpty()) return primaryLines.joinToString("\n")

        val normalizeKey = { s: String ->
            s.lowercase().replace(Regex("[\\s\\p{Punct}]+"), "")
        }
        val existingKeys = primaryLines.map { normalizeKey(it) }.filter { it.isNotEmpty() }.toMutableSet()
        val extraHeaderLines = mutableListOf<String>()
        val extraTailLines = mutableListOf<String>()

        secondaryLines.forEachIndexed { idx, line ->
            val key = normalizeKey(line)
            val isCovered = key.isEmpty() || existingKeys.any {
                it == key || (key.length >= 4 && it.contains(key)) || (it.length >= 4 && key.contains(it))
            }
            if (!isCovered) {
                existingKeys.add(key)
                if (idx == 0 && !line.any { it.isDigit() } && extraHeaderLines.isEmpty()) {
                    extraHeaderLines.add(line)
                } else {
                    extraTailLines.add(line)
                }
            }
        }
        return (extraHeaderLines + primaryLines + extraTailLines).joinToString("\n")
    }

    private fun recognizeImageFile(
        context: Context,
        imageFile: File,
        language: String,
        startTime: Long
    ): OcrResult {
        var rawBitmap: Bitmap? = null
        var orientedBitmap: Bitmap? = null
        var preparedBitmap: Bitmap? = null
        var invertedBitmap: Bitmap? = null
        var tessBaseAPI: TessBaseAPI? = null
        return try {
            rawBitmap = decodeSampledBitmap(imageFile.absolutePath, 2048, 2048)
            if (rawBitmap == null) {
                return OcrResult(success = false, error = "Failed to decode image: ${imageFile.name}")
            }

            orientedBitmap = OcrImagePreprocessor.applyExifOrientation(imageFile.absolutePath, rawBitmap)
            if (orientedBitmap != rawBitmap) {
                rawBitmap = null // recycled inside applyExifOrientation
            }

            val prepared = OcrImagePreprocessor.prepareForTesseract(orientedBitmap, forceInvert = false)
            preparedBitmap = prepared.bitmap

            tessBaseAPI = TessBaseAPI()
            val datapath = context.filesDir.absolutePath
            val initSuccess = tessBaseAPI.init(datapath, language)
            if (!initSuccess) {
                return OcrResult(success = false, error = "Failed to initialize Tesseract engine with language '$language'")
            }

            tessBaseAPI.setVariable("user_defined_dpi", "300")

            // Pass 1: PSM_AUTO on preprocessed (contrast-stretched, upscaled, padded, dark-mode-ready) image
            tessBaseAPI.pageSegMode = TessBaseAPI.PageSegMode.PSM_AUTO
            tessBaseAPI.setImage(preparedBitmap)
            val autoText = (tessBaseAPI.utF8Text ?: "").trim()
            val autoConf = tessBaseAPI.meanConfidence()

            var bestText = autoText
            var bestConf = autoConf
            var usedPsm = "AUTO"
            var usedDarkInvert = prepared.inverted

            val autoNonBlankLines = autoText.lines().count { isMeaningfulOcrLine(it) }

            // Pass 2: PSM_SPARSE_TEXT for business cards & cropped screenshots if Pass 1 looks sparse or uncertain
            if (autoNonBlankLines < 3 || autoText.length < 36 || autoConf < 74) {
                tessBaseAPI.pageSegMode = TessBaseAPI.PageSegMode.PSM_SPARSE_TEXT
                tessBaseAPI.setImage(preparedBitmap)
                val sparseText = (tessBaseAPI.utF8Text ?: "").trim()
                val sparseConf = tessBaseAPI.meanConfidence()
                val sparseLines = sparseText.lines().count { isMeaningfulOcrLine(it) }

                if (sparseLines > autoNonBlankLines || (sparseLines == autoNonBlankLines && sparseText.length > autoText.length && sparseConf >= autoConf - 8)) {
                    bestText = mergeUniqueOcrLines(sparseText, autoText)
                    bestConf = maxOf(autoConf, sparseConf)
                    usedPsm = "SPARSE_FUSED"
                } else if (sparseText.isNotBlank()) {
                    val merged = mergeUniqueOcrLines(autoText, sparseText)
                    if (merged != bestText) {
                        bestText = merged
                        usedPsm = "AUTO+SPARSE"
                    }
                }
            }

            // Pass 3: Mixed-polarity rescue (e.g. white Name on dark blue header + black numbers on white card body)
            val currentLines = bestText.lines().count { isMeaningfulOcrLine(it) }
            if (!prepared.inverted && prepared.statsBefore.darkFraction >= 0.16f && currentLines < 3) {
                val invPrepared = OcrImagePreprocessor.prepareForTesseract(orientedBitmap, forceInvert = true)
                invertedBitmap = invPrepared.bitmap
                tessBaseAPI.pageSegMode = TessBaseAPI.PageSegMode.PSM_SPARSE_TEXT
                tessBaseAPI.setImage(invertedBitmap)
                val invText = (tessBaseAPI.utF8Text ?: "").trim()
                val invConf = tessBaseAPI.meanConfidence()
                val invLines = invText.lines().count { isMeaningfulOcrLine(it) }
                if (invLines > 0 && invConf >= 45) {
                    bestText = mergeUniqueOcrLines(bestText, invText)
                    if (bestText.lines().count { isMeaningfulOcrLine(it) } > currentLines) {
                        usedDarkInvert = true
                        usedPsm = "$usedPsm+BIMODAL"
                    }
                }
            }

            val timeTaken = System.currentTimeMillis() - startTime
            OcrResult(
                success = true,
                text = bestText.trim(),
                confidence = bestConf,
                language = language,
                processingTimeMs = timeTaken,
                pageCount = 1,
                darkModeInverted = usedDarkInvert,
                psmMode = usedPsm
            )
        } catch (e: Exception) {
            Log.e(TAG, "OCR recognition error: ${e.message}", e)
            OcrResult(success = false, error = "OCR error: ${e.message}")
        } finally {
            try {
                tessBaseAPI?.recycle()
                invertedBitmap?.recycle()
                preparedBitmap?.recycle()
                orientedBitmap?.recycle()
                rawBitmap?.recycle()
            } catch (_: Exception) {}
        }
    }

    private fun recognizePdfPages(
        context: Context,
        pdfFile: File,
        language: String,
        startTime: Long,
        maxPages: Int = 8,
        onStatus: ((String) -> Unit)? = null
    ): OcrResult {
        var pfd: ParcelFileDescriptor? = null
        var renderer: PdfRenderer? = null
        var tessBaseAPI: TessBaseAPI? = null
        val fullText = StringBuilder()
        var totalConfidence = 0
        var pagesProcessed = 0

        return try {
            pfd = ParcelFileDescriptor.open(pdfFile, ParcelFileDescriptor.MODE_READ_ONLY)
            renderer = PdfRenderer(pfd)
            val pageCount = renderer.pageCount.coerceAtMost(maxPages)

            tessBaseAPI = TessBaseAPI()
            val datapath = context.filesDir.absolutePath
            if (!tessBaseAPI.init(datapath, language)) {
                return OcrResult(success = false, error = "Failed to initialize Tesseract engine for PDF")
            }
            tessBaseAPI.pageSegMode = TessBaseAPI.PageSegMode.PSM_AUTO

            for (i in 0 until pageCount) {
                onStatus?.invoke("OCR rendering PDF page ${i + 1}/$pageCount ($language)...")
                val page = renderer.openPage(i)
                val width = (page.width * 2.0).toInt().coerceIn(800, 2400)
                val height = (page.height * 2.0).toInt().coerceIn(800, 2400)
                val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
                // Critical: fill bitmap with solid white before PdfRenderer.Page.render()
                // so dark Myanmar/English glyphs have high contrast instead of black-on-transparent
                bitmap.eraseColor(android.graphics.Color.WHITE)
                page.render(bitmap, null, null, PdfRenderer.Page.RENDER_MODE_FOR_PRINT)
                page.close()

                tessBaseAPI.setImage(bitmap)
                val pageText = tessBaseAPI.utF8Text ?: ""
                val conf = tessBaseAPI.meanConfidence()

                if (pageText.isNotBlank()) {
                    if (fullText.isNotEmpty()) fullText.append("\n\n--- Page ${i + 1} ---\n\n")
                    fullText.append(pageText.trim())
                }

                totalConfidence += conf
                pagesProcessed++
                bitmap.recycle()
            }

            val avgConfidence = if (pagesProcessed > 0) totalConfidence / pagesProcessed else 0
            val timeTaken = System.currentTimeMillis() - startTime

            OcrResult(
                success = true,
                text = fullText.toString().trim(),
                confidence = avgConfidence,
                language = language,
                processingTimeMs = timeTaken,
                pageCount = pagesProcessed
            )
        } catch (e: Exception) {
            Log.e(TAG, "PDF OCR error: ${e.message}", e)
            OcrResult(success = false, error = "PDF OCR failed: ${e.message}")
        } finally {
            try {
                tessBaseAPI?.recycle()
                renderer?.close()
                pfd?.close()
            } catch (_: Exception) {}
        }
    }

    private fun decodeSampledBitmap(path: String, reqWidth: Int, reqHeight: Int): Bitmap? {
        val options = BitmapFactory.Options().apply {
            inJustDecodeBounds = true
        }
        BitmapFactory.decodeFile(path, options)

        var inSampleSize = 1
        val height = options.outHeight
        val width = options.outWidth

        if (height > reqHeight || width > reqWidth) {
            val halfHeight = height / 2
            val halfWidth = width / 2
            while ((halfHeight / inSampleSize) >= reqHeight && (halfWidth / inSampleSize) >= reqWidth) {
                inSampleSize *= 2
            }
        }

        val decodeOptions = BitmapFactory.Options().apply {
            this.inSampleSize = inSampleSize
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }
        return BitmapFactory.decodeFile(path, decodeOptions)
    }
}
