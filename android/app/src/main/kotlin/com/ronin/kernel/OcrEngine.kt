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
    private const val GITHUB_TESSDATA_FAST_URL = "https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main"

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
        val error: String? = null
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
            recognizePdfPages(context, file, language, startTime)
        } else {
            recognizeImageFile(context, file, language, startTime)
        }
    }

    private fun recognizeImageFile(
        context: Context,
        imageFile: File,
        language: String,
        startTime: Long
    ): OcrResult {
        var bitmap: Bitmap? = null
        var tessBaseAPI: TessBaseAPI? = null
        return try {
            bitmap = decodeSampledBitmap(imageFile.absolutePath, 2048, 2048)
            if (bitmap == null) {
                return OcrResult(success = false, error = "Failed to decode image: ${imageFile.name}")
            }

            tessBaseAPI = TessBaseAPI()
            val datapath = context.filesDir.absolutePath
            val initSuccess = tessBaseAPI.init(datapath, language)
            if (!initSuccess) {
                return OcrResult(success = false, error = "Failed to initialize Tesseract engine with language '$language'")
            }

            tessBaseAPI.pageSegMode = TessBaseAPI.PageSegMode.PSM_AUTO
            tessBaseAPI.setImage(bitmap)

            val text = tessBaseAPI.utF8Text ?: ""
            val confidence = tessBaseAPI.meanConfidence()
            val timeTaken = System.currentTimeMillis() - startTime

            OcrResult(
                success = true,
                text = text.trim(),
                confidence = confidence,
                language = language,
                processingTimeMs = timeTaken,
                pageCount = 1
            )
        } catch (e: Exception) {
            Log.e(TAG, "OCR recognition error: ${e.message}", e)
            OcrResult(success = false, error = "OCR error: ${e.message}")
        } finally {
            try {
                tessBaseAPI?.recycle()
                bitmap?.recycle()
            } catch (_: Exception) {}
        }
    }

    private fun recognizePdfPages(
        context: Context,
        pdfFile: File,
        language: String,
        startTime: Long,
        maxPages: Int = 3
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

            for (i in 0 until pageCount) {
                val page = renderer.openPage(i)
                val width = (page.width * 1.5).toInt().coerceAtMost(2048)
                val height = (page.height * 1.5).toInt().coerceAtMost(2048)
                val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
                page.render(bitmap, null, null, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
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
