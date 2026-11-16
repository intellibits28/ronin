package com.ronin.kernel

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Matrix
import android.media.ExifInterface
import android.util.Log
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Pure-Kotlin & Android Bitmap preprocessor for Tesseract OCR.
 * Fixes common mobile OCR failure modes on cropped screenshots & business cards:
 * 1. Dark-mode auto-inversion (light text on dark background -> crisp black on white)
 * 2. Percentile contrast stretching (enhances faint gray metadata / colored UI links)
 * 3. Resolution upscaling for small cropped screenshots (targets ~300 DPI glyph height)
 * 4. Quiet-zone white margin padding around cropped edges
 * 5. EXIF rotation correction for camera-captured business cards
 */
object OcrImagePreprocessor {
    private const val TAG = "Ronin_OcrPreprocess"
    private const val MIN_TARGET_LONG_SIDE = 1600
    private const val MAX_UPSCALE_FACTOR = 3.0f
    private const val PADDING_PX = 24

    data class LuminanceStats(
        val medianLuma: Int,
        val borderMedianLuma: Int,
        val darkFraction: Float,
        val brightFraction: Float
    )

    data class PreprocessedPixelsResult(
        val pixels: IntArray,
        val inverted: Boolean,
        val statsBefore: LuminanceStats
    )

    data class PreparedBitmap(
        val bitmap: Bitmap,
        val inverted: Boolean,
        val statsBefore: LuminanceStats,
        val upscaled: Boolean
    )

    fun computeLuminance(argb: Int): Int {
        val r = (argb shr 16) and 0xFF
        val g = (argb shr 8) and 0xFF
        val b = argb and 0xFF
        return (0.299f * r + 0.587f * g + 0.114f * b).roundToInt().coerceIn(0, 255)
    }

    fun analyzeLuminance(pixels: IntArray, width: Int, height: Int): LuminanceStats {
        if (pixels.isEmpty() || width <= 0 || height <= 0) {
            return LuminanceStats(255, 255, 0f, 1f)
        }

        val hist = IntArray(256)
        val borderHist = IntArray(256)
        var darkCount = 0
        var brightCount = 0
        var borderCount = 0

        val borderBandX = (width * 0.06f).roundToInt().coerceAtLeast(1)
        val borderBandY = (height * 0.06f).roundToInt().coerceAtLeast(1)

        for (y in 0 until height) {
            val rowOffset = y * width
            val isYBorder = y < borderBandY || y >= height - borderBandY
            for (x in 0 until width) {
                val luma = computeLuminance(pixels[rowOffset + x])
                hist[luma]++
                if (luma < 110) darkCount++
                if (luma > 180) brightCount++

                if (isYBorder || x < borderBandX || x >= width - borderBandX) {
                    borderHist[luma]++
                    borderCount++
                }
            }
        }

        val total = pixels.size
        val medianLuma = percentileFromHistogram(hist, total, 0.50f)
        val borderMedian = if (borderCount > 0) percentileFromHistogram(borderHist, borderCount, 0.50f) else medianLuma

        return LuminanceStats(
            medianLuma = medianLuma,
            borderMedianLuma = borderMedian,
            darkFraction = darkCount.toFloat() / total.toFloat(),
            brightFraction = brightCount.toFloat() / total.toFloat()
        )
    }

    private fun percentileFromHistogram(hist: IntArray, totalCount: Int, fraction: Float): Int {
        if (totalCount <= 0) return 128
        val target = (totalCount * fraction).roundToInt().coerceIn(1, totalCount)
        var cumulative = 0
        for (bin in 0..255) {
            cumulative += hist[bin]
            if (cumulative >= target) return bin
        }
        return 255
    }

    /**
     * Pure-Kotlin pixel buffer normalization:
     * - Detects dark backgrounds and inverts luminance
     * - Applies 2%-98% contrast stretching so faint UI fonts remain sharp
     */
    fun preprocessPixels(
        pixels: IntArray,
        width: Int,
        height: Int,
        forceInvert: Boolean = false
    ): PreprocessedPixelsResult {
        val stats = analyzeLuminance(pixels, width, height)
        val shouldInvert = forceInvert ||
            (stats.medianLuma < 115 && stats.darkFraction >= 0.48f) ||
            (stats.borderMedianLuma < 105 && stats.darkFraction >= 0.42f)

        val lumaBuffer = IntArray(pixels.size)
        val hist = IntArray(256)

        for (i in pixels.indices) {
            val rawLuma = computeLuminance(pixels[i])
            val luma = if (shouldInvert) 255 - rawLuma else rawLuma
            lumaBuffer[i] = luma
            hist[luma]++
        }

        val lowBin = percentileFromHistogram(hist, pixels.size, 0.02f)
        val highBin = percentileFromHistogram(hist, pixels.size, 0.98f)
        val span = highBin - lowBin

        val outPixels = IntArray(pixels.size)
        for (i in pixels.indices) {
            val v = if (span >= 24) {
                (((lumaBuffer[i] - lowBin) * 255f) / span).roundToInt().coerceIn(0, 255)
            } else {
                lumaBuffer[i]
            }
            outPixels[i] = (0xFF shl 24) or (v shl 16) or (v shl 8) or v
        }

        return PreprocessedPixelsResult(
            pixels = outPixels,
            inverted = shouldInvert,
            statsBefore = stats
        )
    }

    fun applyExifOrientation(imagePath: String, bitmap: Bitmap): Bitmap {
        return try {
            val exif = ExifInterface(imagePath)
            val orientation = exif.getAttributeInt(
                ExifInterface.TAG_ORIENTATION,
                ExifInterface.ORIENTATION_NORMAL
            )
            val matrix = Matrix()
            when (orientation) {
                ExifInterface.ORIENTATION_ROTATE_90 -> matrix.postRotate(90f)
                ExifInterface.ORIENTATION_ROTATE_180 -> matrix.postRotate(180f)
                ExifInterface.ORIENTATION_ROTATE_270 -> matrix.postRotate(270f)
                ExifInterface.ORIENTATION_FLIP_HORIZONTAL -> matrix.preScale(-1f, 1f)
                ExifInterface.ORIENTATION_FLIP_VERTICAL -> matrix.preScale(1f, -1f)
                ExifInterface.ORIENTATION_TRANSPOSE -> {
                    matrix.postRotate(90f)
                    matrix.preScale(-1f, 1f)
                }
                ExifInterface.ORIENTATION_TRANSVERSE -> {
                    matrix.postRotate(270f)
                    matrix.preScale(-1f, 1f)
                }
                else -> return bitmap
            }
            val rotated = Bitmap.createBitmap(bitmap, 0, 0, bitmap.width, bitmap.height, matrix, true)
            if (rotated != bitmap) {
                bitmap.recycle()
            }
            rotated
        } catch (t: Throwable) {
            Log.d(TAG, "EXIF orientation check skipped: ${t.message}")
            bitmap
        }
    }

    fun prepareForTesseract(source: Bitmap, forceInvert: Boolean = false): PreparedBitmap {
        val longSide = max(source.width, source.height)
        val shouldUpscale = longSide in 1 until MIN_TARGET_LONG_SIDE
        val working = if (shouldUpscale) {
            val scale = (MIN_TARGET_LONG_SIDE.toFloat() / longSide.toFloat()).coerceIn(1.0f, MAX_UPSCALE_FACTOR)
            val targetW = (source.width * scale).roundToInt().coerceAtLeast(source.width)
            val targetH = (source.height * scale).roundToInt().coerceAtLeast(source.height)
            Bitmap.createScaledBitmap(source, targetW, targetH, true)
        } else {
            source
        }

        val w = working.width
        val h = working.height
        val pixels = IntArray(w * h)
        working.getPixels(pixels, 0, w, 0, 0, w, h)
        if (working != source) {
            working.recycle()
        }

        val normRes = preprocessPixels(pixels, w, h, forceInvert = forceInvert)
        val normalizedBmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        normalizedBmp.setPixels(normRes.pixels, 0, w, 0, 0, w, h)

        val padded = Bitmap.createBitmap(w + PADDING_PX * 2, h + PADDING_PX * 2, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(padded)
        canvas.drawColor(Color.WHITE)
        canvas.drawBitmap(normalizedBmp, PADDING_PX.toFloat(), PADDING_PX.toFloat(), null)
        normalizedBmp.recycle()

        return PreparedBitmap(
            bitmap = padded,
            inverted = normRes.inverted,
            statsBefore = normRes.statsBefore,
            upscaled = shouldUpscale
        )
    }
}
