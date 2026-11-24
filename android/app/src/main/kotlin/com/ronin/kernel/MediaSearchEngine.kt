package com.ronin.kernel

import android.app.ActivityManager
import android.content.ContentUris
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.BatteryManager
import android.os.Environment
import android.provider.MediaStore
import android.provider.OpenableColumns
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.yield
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sqrt

enum class MediaIndexScope(val label: String) {
    ALL("All Media"),
    PHOTOS("Photos Only"),
    AUDIO("Audio Only"),
    VIDEO("Videos Only")
}

enum class MediaModalityFilter(val sqlFilter: String, val label: String) {
    ALL("ALL", "All Media"),
    VISUAL("VISUAL", "Photos & Video Frames"),
    IMAGE("IMAGE", "Photos"),
    AUDIO("AUDIO", "Audio"),
    VIDEO("VIDEO", "Videos")
}

enum class MediaSearchQueryMode {
    TEXT_TO_MEDIA,
    IMAGE_TO_VISUAL
}

data class DeviceMediaCounts(
    val totalPhotos: Int = 0,
    val nonScreenshotPhotos: Int = 0,
    val screenshots: Int = 0,
    val audio: Int = 0,
    val videos: Int = 0
)

data class IndexedMediaEntry(
    val mediaPath: String,
    val mediaUri: String,
    val modality: String,
    val displayName: String,
    val durationMs: Long,
    val modifiedAtSec: Long,
    val segmentCount: Int,
    val lastIndexedSec: Long,
    val modelId: String = ""
)

data class DeviceMediaAsset(
    val mediaPath: String,
    val mediaUri: String,
    val modality: String, // "IMAGE", "AUDIO", "VIDEO"
    val displayName: String,
    val folderLabel: String,
    val modifiedAtSec: Long,
    val fileSize: Long,
    val durationMs: Long
)

data class MediaSearchResultItem(
    val mediaPath: String,
    val mediaUri: String,
    val modality: String, // Canonical: "image", "audio", "text"
    val displayName: String,
    val startMs: Long,
    val endMs: Long,
    val durationMs: Long,
    val score: Float,
    val extraMomentsMs: List<Long> = emptyList(),
    val modelId: String = "",
    val cosineScore: Float = score,
    val zScore: Float = 0f,
    val corpusMean: Float = 0f,
    val corpusStd: Float = 0f,
    val corpusSize: Int = 0,
    val lexicalHits: Int = 0,
    val assetType: String = "" // Source container: "image", "audio", "video", "text"
)

data class MediaIndexProgress(
    val currentIndex: Int,
    val totalCount: Int,
    val currentFileName: String,
    val statusText: String
)

data class MediaIndexSummary(
    val success: Boolean,
    val modelId: String,
    val supportsVision: Boolean,
    val supportsAudio: Boolean,
    val indexedImages: Int = 0,
    val indexedAudio: Int = 0,
    val indexedVideos: Int = 0,
    val skippedUnchanged: Int = 0,
    val failedCount: Int = 0,
    val cancelled: Boolean = false,
    val stoppedForGuardrail: String? = null,
    val error: String? = null
)

data class MediaSearchOutcome(
    val success: Boolean,
    val query: String,
    val filter: MediaModalityFilter,
    val modelId: String = "",
    val results: List<MediaSearchResultItem> = emptyList(),
    val indexEmpty: Boolean = false,
    val error: String? = null,
    val scannedCount: Int = 0,
    val bestRejectedScore: Float = 0f
)

data class ParsedMediaQuery(
    val query: String,
    val filter: MediaModalityFilter
)

/**
 * Encodes mono 16 kHz 16-bit signed LE PCM samples into a canonical 44-byte RIFF/WAVE payload
 * accepted by LiteRT-LM `InputData.Audio`.
 */
object WavEncoder {
    fun encodeMono16kWav(pcmSamples: ShortArray, sampleRate: Int = 16000): ByteArray {
        val dataSize = pcmSamples.size * 2
        val totalSize = 44 + dataSize
        val buffer = ByteBuffer.allocate(totalSize).order(ByteOrder.LITTLE_ENDIAN)

        // RIFF chunk descriptor
        buffer.put('R'.code.toByte())
        buffer.put('I'.code.toByte())
        buffer.put('F'.code.toByte())
        buffer.put('F'.code.toByte())
        buffer.putInt(36 + dataSize)
        buffer.put('W'.code.toByte())
        buffer.put('A'.code.toByte())
        buffer.put('V'.code.toByte())
        buffer.put('E'.code.toByte())

        // fmt subchunk
        buffer.put('f'.code.toByte())
        buffer.put('m'.code.toByte())
        buffer.put('t'.code.toByte())
        buffer.put(' '.code.toByte())
        buffer.putInt(16) // PCM subchunk size
        buffer.putShort(1.toShort()) // AudioFormat = 1 (PCM)
        buffer.putShort(1.toShort()) // NumChannels = 1 (Mono)
        buffer.putInt(sampleRate)
        buffer.putInt(sampleRate * 2) // ByteRate = SampleRate * Channels * BitsPerSample/8
        buffer.putShort(2.toShort()) // BlockAlign = Channels * BitsPerSample/8
        buffer.putShort(16.toShort()) // BitsPerSample = 16

        // data subchunk
        buffer.put('d'.code.toByte())
        buffer.put('a'.code.toByte())
        buffer.put('t'.code.toByte())
        buffer.put('a'.code.toByte())
        buffer.putInt(dataSize)
        for (sample in pcmSamples) {
            buffer.putShort(sample)
        }
        return buffer.array()
    }
}

/**
 * Pure-Kotlin PCM downmixer, linear resampler (to 16 kHz mono), and sliding-window slicer.
 */
object AudioPcmResampler {
    data class AudioWindow(
        val index: Int,
        val startMs: Long,
        val endMs: Long,
        val samples: ShortArray
    )

    fun resampleAndDownmixToMono16k(
        interleavedPcm: ShortArray,
        sampleCount: Int,
        sourceSampleRate: Int,
        channelCount: Int
    ): ShortArray {
        if (sampleCount <= 0 || interleavedPcm.isEmpty() || sourceSampleRate <= 0 || channelCount <= 0) {
            return ShortArray(0)
        }
        val usableCount = min(sampleCount, interleavedPcm.size)
        val frameCount = usableCount / channelCount
        if (frameCount <= 0) return ShortArray(0)

        val monoSource = ShortArray(frameCount)
        if (channelCount == 1) {
            System.arraycopy(interleavedPcm, 0, monoSource, 0, frameCount)
        } else {
            for (f in 0 until frameCount) {
                var sum = 0L
                val base = f * channelCount
                for (ch in 0 until channelCount) {
                    sum += interleavedPcm[base + ch]
                }
                monoSource[f] = (sum / channelCount).toInt().coerceIn(-32768, 32767).toShort()
            }
        }

        if (sourceSampleRate == 16000) {
            return monoSource
        }

        val targetCount = max(1, ((frameCount.toLong() * 16000L) / sourceSampleRate.toLong()).toInt())
        val out = ShortArray(targetCount)
        val ratio = sourceSampleRate.toDouble() / 16000.0
        for (i in 0 until targetCount) {
            val srcPos = i * ratio
            val idx0 = srcPos.toInt().coerceIn(0, frameCount - 1)
            val idx1 = min(idx0 + 1, frameCount - 1)
            val frac = srcPos - idx0
            val interp = monoSource[idx0] * (1.0 - frac) + monoSource[idx1] * frac
            out[i] = interp.roundToInt().coerceIn(-32768, 32767).toShort()
        }
        return out
    }

    fun computeRms(samples: ShortArray, start: Int = 0, end: Int = samples.size): Double {
        if (end <= start) return 0.0
        var sumSq = 0.0
        for (i in start until end) {
            val s = samples[i].toDouble()
            sumSq += s * s
        }
        return sqrt(sumSq / (end - start).toDouble())
    }

    fun sliceAudioWindows(
        mono16k: ShortArray,
        windowSec: Int = 20,
        stepSec: Int = 15,
        maxWindows: Int = 12,
        sampleRate: Int = 16000
    ): List<AudioWindow> {
        if (mono16k.isEmpty() || sampleRate <= 0 || windowSec <= 0 || stepSec <= 0 || maxWindows <= 0) {
            return emptyList()
        }
        val windowSamples = windowSec * sampleRate
        val stepSamples = stepSec * sampleRate
        val allWindows = mutableListOf<AudioWindow>()

        var offset = 0
        var idx = 0
        while (offset < mono16k.size && allWindows.size < maxWindows) {
            val end = min(mono16k.size, offset + windowSamples)
            val slice = mono16k.copyOfRange(offset, end)
            val startMs = (offset.toLong() * 1000L) / sampleRate.toLong()
            val endMs = (end.toLong() * 1000L) / sampleRate.toLong()
            allWindows.add(AudioWindow(idx, startMs, endMs, slice))
            idx++
            if (end >= mono16k.size) break
            offset += stepSamples
        }

        val nonSilent = allWindows.filter { computeRms(it.samples) >= 25.0 }
        return when {
            nonSilent.isNotEmpty() -> nonSilent.mapIndexed { newIdx, w -> w.copy(index = newIdx) }
            allWindows.isNotEmpty() -> listOf(allWindows.first().copy(index = 0))
            else -> emptyList()
        }
    }
}

/**
 * Pure-Kotlin video frame sampling scheduler balancing moment coverage against mobile latency.
 */
object MediaFrameScheduler {
    fun computeSampleTimestampsMs(durationMs: Long): List<Long> {
        if (durationMs <= 0L) return listOf(0L)
        if (durationMs <= 1200L) return listOf(0L)

        val timestamps = mutableListOf<Long>()
        when {
            durationMs <= 30_000L -> {
                var t = 500L
                val maxT = durationMs - 200L
                while (t <= maxT && timestamps.size < 15) {
                    timestamps.add(t)
                    t += 2_000L
                }
            }
            durationMs <= 180_000L -> {
                var t = 500L
                val maxT = durationMs - 500L
                while (t <= maxT && timestamps.size < 24) {
                    timestamps.add(t)
                    t += 5_000L
                }
            }
            else -> {
                val count = 30
                val startT = 500L
                val span = max(1L, durationMs - 1_000L)
                for (i in 0 until count) {
                    val t = startT + (span * i) / (count - 1)
                    timestamps.add(t)
                }
            }
        }
        return if (timestamps.isEmpty()) listOf(0L) else timestamps.distinct()
    }
}

/**
 * Parses natural-language Burmese, English, and Burm-glish media search requests while
 * avoiding false positives on contact, SMS, vault, OCR, SHM, or tuner intents.
 */
object MediaIntentParser {
    private val EXCLUDED_KEYWORDS = listOf(
        "contact", "contacts", "phonebook", "business card", "အဆက်အသွယ်", "ဖုန်းနံပါတ်",
        "sms", "send message", "မက်ဆေ့", "vault", "password", "api key", "secret",
        "ocr", "extract text", "read text", "စာဖတ်", "summarize", "summary", "အကျဉ်းချုပ်",
        "guitar", "tuner", "tune", "ဂစ်တာ", "vibration", "shm", "resonance", "တုန်ခါမှု"
    )

    private val PHOTO_MARKERS = listOf("photo", "photos", "image", "images", "picture", "pictures", "pic", "pics", "screenshot", "screenshots", "ဓာတ်ပုံ", "ပုံတွေ", "ပုံ")
    private val AUDIO_MARKERS = listOf("audio", "voice", "voice memo", "voice note", "recording", "recordings", "sound", "song", "အသံဖိုင်", "အသံတွေ", "အသံ", "သီချင်း")
    private val VIDEO_MARKERS = listOf("video", "videos", "clip", "clips", "movie", "movies", "moment", "moments", "ဗီဒီယို", "ဗွီဒီယို")
    private val SEARCH_VERBS = listOf(
        "find", "search", "show", "locate", "look for", "where", "which",
        "ရှာ", "ပြ", "ကြည့်", "ဘယ်နား", "ဘယ်မှာ", "ဘယ်တစ်ခု", "ဖွင့်"
    )

    private fun containsToken(text: String, keyword: String): Boolean {
        return if (keyword.any { it in '\u1000'..'\u109F' }) {
            text.contains(keyword)
        } else {
            Regex("""\b${Regex.escape(keyword)}\b""", RegexOption.IGNORE_CASE).containsMatchIn(text)
        }
    }

    fun parseNaturalMediaSearchIntent(rawInput: String): ParsedMediaQuery? {
        val trimmed = rawInput.trim()
        if (trimmed.isEmpty() || trimmed.startsWith("/")) return null
        val lower = trimmed.lowercase()

        if (EXCLUDED_KEYWORDS.any { containsToken(lower, it) }) {
            return null
        }

        val hasPhoto = PHOTO_MARKERS.any { containsToken(lower, it) }
        val hasAudio = AUDIO_MARKERS.any { containsToken(lower, it) }
        val hasVideo = VIDEO_MARKERS.any { containsToken(lower, it) }
        if (!hasPhoto && !hasAudio && !hasVideo) return null

        val hasSearchVerb = SEARCH_VERBS.any { containsToken(lower, it) }
        val hasPhoneMediaContext = lower.contains("ဖုန်းထဲက") ||
            lower.contains("ဖုန်းထဲ") ||
            lower.contains("gallery") ||
            lower.contains("video ထဲက") ||
            lower.contains("အသံထဲက") ||
            lower.contains("on my phone")
        if (!hasSearchVerb && !hasPhoneMediaContext) return null

        val filter = when {
            hasVideo && !hasPhoto && !hasAudio -> MediaModalityFilter.VIDEO
            hasAudio && !hasPhoto && !hasVideo -> MediaModalityFilter.AUDIO
            hasPhoto && !hasVideo && !hasAudio -> MediaModalityFilter.IMAGE
            hasVideo -> MediaModalityFilter.VIDEO
            else -> MediaModalityFilter.ALL
        }

        var query = trimmed
            .replace(Regex("""(?i)\b(?:please|kindly|can\s+you|find|search|show|locate|look\s+for|get|me|my|all|on|in|from|the|phone|device|gallery|of|with|about|where|moment|moments|clip|clips|part)\b"""), " ")
            .replace(Regex("""(?i)\b(?:photos?|images?|pictures?|pics?|screenshots?|videos?|movies?|audio|voice\s*(?:memo|note)s?|recordings?|sounds?|songs?|files?)\b"""), " ")
            .replace(Regex("""(?:ဖုန်းထဲက|ဖုန်းထဲမှာ|ဖုန်းထဲ|gallery\s*ထဲက|ဗီဒီယိုထဲက|ဗွီဒီယိုထဲက|video\s*ထဲက|အသံထဲက|ပုံထဲက)"""), " ")
            .replace(Regex("""(?:ရှာပေးပါ|ရှာပေး|ရှာကြည့်|ရှာပါ|ပြပေးပါ|ပြပေး|ပြပါ|ဖွင့်ပေးပါ|ပေးပါ)"""), " ")
            .replace(Regex("""(?:ဓာတ်ပုံတွေ|ဓာတ်ပုံ|ပုံတွေ|ပုံ|ဗီဒီယိုတွေ|ဗီဒီယို|ဗွီဒီယို|အသံဖိုင်တွေ|အသံဖိုင်|အသံတွေ|အသံ|သီချင်းတွေ|သီချင်း|ဟာတွေ|ဟာ|တွေ)"""), " ")
            .replace(Regex("""(?<=\A|\s)(?:ပါ|က|ကို|မှာ|ထဲ|နဲ့)(?=\s|\z)"""), " ")
            .replace(Regex("""\s+"""), " ")
            .trim(' ', '.', ',', '?', '!', '။', '၊')

        if (query.isBlank()) {
            query = trimmed
        }
        return ParsedMediaQuery(query = query, filter = filter)
    }
}

private val CAMERA_ALBUM_LABELS = setOf(
    "camera", "dcim", "pictures", "photos", "100andro", "100media", "opencamera", "restored"
)

fun isScreenshotAsset(mediaPath: String, folderLabel: String, displayName: String): Boolean {
    val pathLower = mediaPath.lowercase()
    val folderLower = folderLabel.trim().lowercase()
    val nameLower = displayName.trim().lowercase()
    return folderLower.contains("screenshot") ||
        pathLower.contains("/screenshots/") ||
        pathLower.contains("/screenshot/") ||
        nameLower.startsWith("screenshot_") ||
        nameLower.startsWith("screen_shot")
}

fun isIgnoredMediaPath(mediaPath: String): Boolean {
    val lower = mediaPath.lowercase()
    return lower.contains("/android/data/") ||
        lower.contains("/android/obb/") ||
        lower.contains("/.thumbnails/") ||
        lower.contains("/thumbnails/") ||
        lower.contains("/cache/") ||
        lower.contains("/.statuses/") ||
        lower.contains("stickers/")
}

/**
 * Orders discovered assets so Camera/Photo albums appear ahead of Downloads/Social folders
 * and Screenshots appear last (or excluded when [excludeScreenshots] is enabled).
 */
fun prioritizeMediaAssets(
    assets: List<DeviceMediaAsset>,
    excludeScreenshots: Boolean = false
): List<DeviceMediaAsset> {
    return assets
        .filter { asset ->
            !(excludeScreenshots && asset.modality == "IMAGE" &&
                isScreenshotAsset(asset.mediaPath, asset.folderLabel, asset.displayName))
        }
        .sortedWith(
            compareBy<DeviceMediaAsset> { asset ->
                if (asset.modality != "IMAGE") {
                    1
                } else if (isScreenshotAsset(asset.mediaPath, asset.folderLabel, asset.displayName)) {
                    2
                } else {
                    val folderLower = asset.folderLabel.trim().lowercase()
                    val pathLower = asset.mediaPath.lowercase()
                    if (folderLower in CAMERA_ALBUM_LABELS || pathLower.contains("/dcim/camera/")) 0 else 1
                }
            }.thenByDescending { it.modifiedAtSec }
        )
}

/**
 * Defensive boundary filter ensuring zero cross-modal or cross-asset leakage in final results.
 */
fun enforceStrictModalityIsolation(
    results: List<MediaSearchResultItem>,
    allowedModality: String,
    allowedAssetType: String? = null
): List<MediaSearchResultItem> {
    val normMod = allowedModality.trim().lowercase()
    val normAsset = allowedAssetType?.trim()?.lowercase()?.takeIf { it.isNotEmpty() }
    if (normMod.isEmpty()) return emptyList()
    return results.filter { item ->
        val rawMod = item.modality.trim().lowercase()
        val canonicalMod = when (rawMod) {
            "video_frame" -> "image"
            "video_audio" -> "audio"
            else -> rawMod
        }
        val resolvedAsset = item.assetType.trim().lowercase().ifEmpty {
            when (rawMod) {
                "video_frame", "video_audio" -> "video"
                "image", "audio", "text" -> rawMod
                else -> ""
            }
        }
        canonicalMod == normMod && (normAsset == null || resolvedAsset == normAsset)
    }
}

/**
 * Filters out anisotropic background noise using C++ corpus-wide Z-score statistics
 * combined with modality-aware cosine floors.
 */
fun filterRelevantMediaMatches(
    candidates: List<MediaSearchResultItem>,
    queryMode: MediaSearchQueryMode = MediaSearchQueryMode.TEXT_TO_MEDIA
): List<MediaSearchResultItem> {
    if (candidates.isEmpty()) return emptyList()
    return candidates.filter { item ->
        if (item.cosineScore <= 0f && item.lexicalHits > 0 && item.score > 0f) {
            return@filter true
        }
        if (item.lexicalHits > 0 && item.cosineScore >= 0.62f) {
            return@filter true
        }

        val isAudioSeg = item.modality.equals("audio", ignoreCase = true) ||
            item.modality.equals("VIDEO_AUDIO", ignoreCase = true)
        val highFloor: Float
        val adaptiveFloor: Float
        val minZ: Float
        val minMargin: Float
        when {
            queryMode == MediaSearchQueryMode.IMAGE_TO_VISUAL -> {
                highFloor = 0.76f
                adaptiveFloor = 0.71f
                minZ = 1.4f
                minMargin = 0.04f
            }
            isAudioSeg -> {
                highFloor = 0.66f
                adaptiveFloor = 0.58f
                minZ = 1.3f
                minMargin = 0.04f
            }
            else -> {
                highFloor = 0.715f
                adaptiveFloor = 0.692f
                minZ = 1.15f
                minMargin = 0.035f
            }
        }

        when {
            item.cosineScore >= highFloor -> true
            item.cosineScore >= adaptiveFloor -> when {
                item.corpusSize >= 12 -> item.zScore >= minZ && (item.cosineScore - item.corpusMean) >= minMargin
                item.corpusSize >= 3 -> (item.cosineScore - item.corpusMean) >= (minMargin + 0.015f)
                else -> true
            }
            else -> false
        }
    }
}

/**
 * Pure-Kotlin deduplicator grouping segment hits by media file while preserving primary and secondary timestamps.
 */
fun deduplicateAndRankMediaMatches(
    rawMatches: List<MediaSearchResultItem>,
    topK: Int = 8
): List<MediaSearchResultItem> {
    if (rawMatches.isEmpty() || topK <= 0) return emptyList()
    val grouped = rawMatches.groupBy { it.mediaPath }
    val collapsed = grouped.values.map { segs ->
        val sorted = segs.sortedByDescending { it.score }
        val best = sorted.first()
        val extras = mutableListOf<Long>()
        for (cand in sorted.drop(1)) {
            if (extras.size >= 2) break
            val distinctFromBest = abs(cand.startMs - best.startMs) >= 4_000L
            val distinctFromExtras = extras.all { abs(cand.startMs - it) >= 4_000L }
            if (cand.startMs > 0L && distinctFromBest && distinctFromExtras) {
                extras.add(cand.startMs)
            }
        }
        best.copy(extraMomentsMs = extras)
    }
    return collapsed.sortedByDescending { it.score }.take(topK)
}

object MediaSearchEngine {
    private const val TAG = "RoninMediaSearch"
    const val PHOTO_MAX_SIDE_PX = 448
    const val PHOTO_VISION_TOKENS = 70
    const val PHOTO_BATCH_SIZE = 4
    const val VIDEO_FRAME_MAX_SIDE_PX = 448
    const val VIDEO_FRAME_VISION_TOKENS = 70

    fun formatTimestampMs(ms: Long): String {
        val totalSec = (ms / 1000L).coerceAtLeast(0L)
        val hours = totalSec / 3600L
        val mins = (totalSec % 3600L) / 60L
        val secs = totalSec % 60L
        return if (hours > 0L) {
            "%d:%02d:%02d".format(hours, mins, secs)
        } else {
            "%02d:%02d".format(mins, secs)
        }
    }

    suspend fun countDeviceMedia(context: Context): DeviceMediaCounts = withContext(Dispatchers.IO) {
        var totalPhotos = 0
        var nonScreenshotPhotos = 0
        var screenshots = 0
        var audio = 0
        var videos = 0

        fun countUri(contentUri: Uri, modality: String) {
            val cols = arrayOf(
                MediaStore.MediaColumns.DATA,
                MediaStore.MediaColumns.DISPLAY_NAME,
                MediaStore.MediaColumns.SIZE
            )
            try {
                context.contentResolver.query(contentUri, cols, null, null, null)?.use { cursor ->
                    val dataCol = cursor.getColumnIndex(MediaStore.MediaColumns.DATA)
                    val nameCol = cursor.getColumnIndex(MediaStore.MediaColumns.DISPLAY_NAME)
                    val sizeCol = cursor.getColumnIndex(MediaStore.MediaColumns.SIZE)
                    while (cursor.moveToNext()) {
                        val size = if (sizeCol >= 0) cursor.getLong(sizeCol) else 0L
                        if (size in 1L..2048L) continue
                        val path = if (dataCol >= 0) cursor.getString(dataCol) ?: "" else ""
                        if (path.isNotBlank() && isIgnoredMediaPath(path)) continue
                        val name = if (nameCol >= 0) cursor.getString(nameCol) ?: "" else ""
                        val folder = File(path).parentFile?.name ?: ""
                        when (modality) {
                            "IMAGE" -> {
                                totalPhotos++
                                if (isScreenshotAsset(path, folder, name)) screenshots++ else nonScreenshotPhotos++
                            }
                            "AUDIO" -> audio++
                            "VIDEO" -> videos++
                        }
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "countDeviceMedia failed for $modality: ${e.message}")
            }
        }

        countUri(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, "IMAGE")
        countUri(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, "AUDIO")
        countUri(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, "VIDEO")

        DeviceMediaCounts(
            totalPhotos = totalPhotos,
            nonScreenshotPhotos = nonScreenshotPhotos,
            screenshots = screenshots,
            audio = audio,
            videos = videos
        )
    }

    suspend fun discoverDeviceMedia(
        context: Context,
        scope: MediaIndexScope = MediaIndexScope.ALL,
        maxPerModality: Int = 5000,
        excludeScreenshots: Boolean = false
    ): List<DeviceMediaAsset> = withContext(Dispatchers.IO) {
        val assets = mutableListOf<DeviceMediaAsset>()
        val seenPaths = mutableSetOf<String>()

        fun queryStore(contentUri: Uri, modality: String, hasDuration: Boolean) {
            val collectedBefore = assets.count { it.modality == modality }
            val cols = buildList {
                add(MediaStore.MediaColumns._ID)
                add(MediaStore.MediaColumns.DATA)
                add(MediaStore.MediaColumns.DISPLAY_NAME)
                add(MediaStore.MediaColumns.DATE_MODIFIED)
                add(MediaStore.MediaColumns.SIZE)
                if (hasDuration) add(MediaStore.MediaColumns.DURATION)
            }.toTypedArray()

            try {
                context.contentResolver.query(
                    contentUri,
                    cols,
                    null,
                    null,
                    "${MediaStore.MediaColumns.DATE_MODIFIED} DESC"
                )?.use { cursor ->
                    val idCol = cursor.getColumnIndex(MediaStore.MediaColumns._ID)
                    val dataCol = cursor.getColumnIndex(MediaStore.MediaColumns.DATA)
                    val nameCol = cursor.getColumnIndex(MediaStore.MediaColumns.DISPLAY_NAME)
                    val modCol = cursor.getColumnIndex(MediaStore.MediaColumns.DATE_MODIFIED)
                    val sizeCol = cursor.getColumnIndex(MediaStore.MediaColumns.SIZE)
                    val durCol = if (hasDuration) cursor.getColumnIndex(MediaStore.MediaColumns.DURATION) else -1

                    var added = 0
                    while (cursor.moveToNext() && (collectedBefore + added) < maxPerModality) {
                        val id = if (idCol >= 0) cursor.getLong(idCol) else -1L
                        val itemUri = if (id >= 0) ContentUris.withAppendedId(contentUri, id).toString() else ""
                        val path = if (dataCol >= 0) cursor.getString(dataCol) ?: "" else ""
                        val key = path.ifBlank { itemUri }
                        if (key.isBlank() || isIgnoredMediaPath(key)) continue

                        val name = (if (nameCol >= 0) cursor.getString(nameCol) else null)
                            ?: File(path).name.ifBlank { "$modality-$id" }
                        val modSec = if (modCol >= 0) cursor.getLong(modCol) else 0L
                        val fileSize = if (sizeCol >= 0) cursor.getLong(sizeCol) else 0L
                        val durationMs = if (durCol >= 0) cursor.getLong(durCol).coerceAtLeast(0L) else 0L

                        // Filter out tiny UI icons or zero-byte artifacts
                        if (fileSize in 1L..2048L) continue
                        val folderLabel = File(path).parentFile?.name ?: ""
                        if (excludeScreenshots && modality == "IMAGE" && isScreenshotAsset(key, folderLabel, name)) {
                            continue
                        }
                        if (!seenPaths.add(key)) continue
                        assets.add(
                            DeviceMediaAsset(
                                mediaPath = key,
                                mediaUri = itemUri,
                                modality = modality,
                                displayName = name,
                                folderLabel = folderLabel,
                                modifiedAtSec = modSec,
                                fileSize = fileSize,
                                durationMs = durationMs
                            )
                        )
                        added++
                    }
                }
            } catch (e: SecurityException) {
                Log.w(TAG, "MediaStore query denied for $modality: ${e.message}")
            } catch (e: Exception) {
                Log.w(TAG, "MediaStore query failed for $modality: ${e.message}")
            }
        }

        if (scope == MediaIndexScope.ALL || scope == MediaIndexScope.PHOTOS) {
            queryStore(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, "IMAGE", hasDuration = false)
        }
        if (scope == MediaIndexScope.ALL || scope == MediaIndexScope.AUDIO) {
            queryStore(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, "AUDIO", hasDuration = true)
        }
        if (scope == MediaIndexScope.ALL || scope == MediaIndexScope.VIDEO) {
            queryStore(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, "VIDEO", hasDuration = true)
        }

        // Fallback filesystem scan when MediaStore is restricted or empty (e.g. MANAGE_EXTERNAL_STORAGE granted)
        if (assets.isEmpty()) {
            try {
                val root = Environment.getExternalStorageDirectory()
                val dirs = listOf("DCIM", "Pictures", "Movies", "Recordings", "Music", "Download")
                    .map { File(root, it) }
                    .filter { it.exists() && it.canRead() }
                val imgExts = setOf("jpg", "jpeg", "png", "webp")
                val audExts = setOf("m4a", "mp3", "wav", "ogg", "aac", "flac", "opus")
                val vidExts = setOf("mp4", "mkv", "mov", "webm", "3gp")

                for (dir in dirs) {
                    dir.walkTopDown().maxDepth(4).forEach { f ->
                        if (!f.isFile || f.length() <= 2048L || isIgnoredMediaPath(f.absolutePath)) return@forEach
                        val ext = f.extension.lowercase()
                        val modality = when {
                            ext in imgExts && (scope == MediaIndexScope.ALL || scope == MediaIndexScope.PHOTOS) -> "IMAGE"
                            ext in audExts && (scope == MediaIndexScope.ALL || scope == MediaIndexScope.AUDIO) -> "AUDIO"
                            ext in vidExts && (scope == MediaIndexScope.ALL || scope == MediaIndexScope.VIDEO) -> "VIDEO"
                            else -> return@forEach
                        }
                        val folderLabel = f.parentFile?.name ?: ""
                        if (excludeScreenshots && modality == "IMAGE" && isScreenshotAsset(f.absolutePath, folderLabel, f.name)) {
                            return@forEach
                        }
                        if (assets.count { it.modality == modality } >= maxPerModality) return@forEach
                        if (seenPaths.add(f.absolutePath)) {
                            assets.add(
                                DeviceMediaAsset(
                                    mediaPath = f.absolutePath,
                                    mediaUri = Uri.fromFile(f).toString(),
                                    modality = modality,
                                    displayName = f.name,
                                    folderLabel = folderLabel,
                                    modifiedAtSec = f.lastModified() / 1000L,
                                    fileSize = f.length(),
                                    durationMs = 0L
                                )
                            )
                        }
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "Filesystem media scan fallback note: ${e.message}")
            }
        }

        prioritizeMediaAssets(assets, excludeScreenshots = excludeScreenshots)
    }

    /**
     * Resolves user-selected photo/video/audio URIs (including temporary Photo Picker URIs)
     * back to permanent MediaStore paths and URIs whenever available.
     */
    suspend fun resolvePickedUrisToAssets(
        context: Context,
        uris: List<Uri>
    ): List<DeviceMediaAsset> = withContext(Dispatchers.IO) {
        val resolved = mutableListOf<DeviceMediaAsset>()
        val seenKeys = mutableSetOf<String>()

        fun lookupPermanentMediaStoreEntry(
            contentUri: Uri,
            displayName: String,
            sizeBytes: Long,
            hasDuration: Boolean
        ): DeviceMediaAsset? {
            if (displayName.isBlank()) return null
            val cols = buildList {
                add(MediaStore.MediaColumns._ID)
                add(MediaStore.MediaColumns.DATA)
                add(MediaStore.MediaColumns.DISPLAY_NAME)
                add(MediaStore.MediaColumns.DATE_MODIFIED)
                add(MediaStore.MediaColumns.SIZE)
                if (hasDuration) add(MediaStore.MediaColumns.DURATION)
            }.toTypedArray()
            val selection = if (sizeBytes > 0L) {
                "${MediaStore.MediaColumns.DISPLAY_NAME} = ? AND ${MediaStore.MediaColumns.SIZE} = ?"
            } else {
                "${MediaStore.MediaColumns.DISPLAY_NAME} = ?"
            }
            val args = if (sizeBytes > 0L) arrayOf(displayName, sizeBytes.toString()) else arrayOf(displayName)
            val modality = when (contentUri) {
                MediaStore.Video.Media.EXTERNAL_CONTENT_URI -> "VIDEO"
                MediaStore.Audio.Media.EXTERNAL_CONTENT_URI -> "AUDIO"
                else -> "IMAGE"
            }
            return try {
                context.contentResolver.query(
                    contentUri,
                    cols,
                    selection,
                    args,
                    "${MediaStore.MediaColumns.DATE_MODIFIED} DESC"
                )?.use { c ->
                    if (!c.moveToFirst()) return@use null
                    val idCol = c.getColumnIndex(MediaStore.MediaColumns._ID)
                    val dataCol = c.getColumnIndex(MediaStore.MediaColumns.DATA)
                    val nameCol = c.getColumnIndex(MediaStore.MediaColumns.DISPLAY_NAME)
                    val modCol = c.getColumnIndex(MediaStore.MediaColumns.DATE_MODIFIED)
                    val sizeCol = c.getColumnIndex(MediaStore.MediaColumns.SIZE)
                    val durCol = if (hasDuration) c.getColumnIndex(MediaStore.MediaColumns.DURATION) else -1
                    val id = if (idCol >= 0) c.getLong(idCol) else -1L
                    val itemUri = if (id >= 0) ContentUris.withAppendedId(contentUri, id).toString() else ""
                    val path = if (dataCol >= 0) c.getString(dataCol) ?: "" else ""
                    val name = (if (nameCol >= 0) c.getString(nameCol) else null) ?: displayName
                    val modSec = if (modCol >= 0) c.getLong(modCol) else 0L
                    val sz = if (sizeCol >= 0) c.getLong(sizeCol) else sizeBytes
                    val dur = if (durCol >= 0) c.getLong(durCol).coerceAtLeast(0L) else 0L
                    val key = path.ifBlank { itemUri }
                    if (key.isBlank()) return@use null
                    DeviceMediaAsset(
                        mediaPath = key,
                        mediaUri = itemUri,
                        modality = modality,
                        displayName = name,
                        folderLabel = File(path).parentFile?.name ?: "",
                        modifiedAtSec = modSec,
                        fileSize = sz,
                        durationMs = dur
                    )
                }
            } catch (_: Throwable) {
                null
            }
        }

        for (uri in uris) {
            var displayName = ""
            var sizeBytes = 0L
            var directPath = ""
            var modifiedSec = 0L
            var durationMs = 0L
            try {
                context.contentResolver.query(uri, null, null, null, null)?.use { c ->
                    if (c.moveToFirst()) {
                        val nameIdx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                        val sizeIdx = c.getColumnIndex(OpenableColumns.SIZE)
                        val dataIdx = c.getColumnIndex(MediaStore.MediaColumns.DATA)
                        val modIdx = c.getColumnIndex(MediaStore.MediaColumns.DATE_MODIFIED)
                        val durIdx = c.getColumnIndex(MediaStore.MediaColumns.DURATION)
                        if (nameIdx >= 0) displayName = c.getString(nameIdx) ?: ""
                        if (sizeIdx >= 0) sizeBytes = c.getLong(sizeIdx)
                        if (dataIdx >= 0) directPath = c.getString(dataIdx) ?: ""
                        if (modIdx >= 0) modifiedSec = c.getLong(modIdx)
                        if (durIdx >= 0) durationMs = c.getLong(durIdx).coerceAtLeast(0L)
                    }
                }
            } catch (_: Throwable) {}

            val mime = runCatching { context.contentResolver.getType(uri) }.getOrNull().orEmpty().lowercase()
            val nameExt = displayName.substringAfterLast('.', "").lowercase()
            val modality = when {
                mime.startsWith("video/") || nameExt in setOf("mp4", "mkv", "mov", "webm", "3gp") -> "VIDEO"
                mime.startsWith("audio/") || nameExt in setOf("m4a", "mp3", "wav", "ogg", "aac", "flac", "opus") -> "AUDIO"
                else -> "IMAGE"
            }

            val directFile = if (directPath.isNotBlank()) File(directPath) else null
            val candidateAsset: DeviceMediaAsset? = if (directFile != null && directFile.exists() && directFile.canRead()) {
                DeviceMediaAsset(
                    mediaPath = directFile.absolutePath,
                    mediaUri = uri.toString(),
                    modality = modality,
                    displayName = displayName.ifBlank { directFile.name },
                    folderLabel = directFile.parentFile?.name ?: "",
                    modifiedAtSec = if (modifiedSec > 0L) modifiedSec else directFile.lastModified() / 1000L,
                    fileSize = if (sizeBytes > 0L) sizeBytes else directFile.length(),
                    durationMs = durationMs
                )
            } else {
                val targetStoreUri = when (modality) {
                    "VIDEO" -> MediaStore.Video.Media.EXTERNAL_CONTENT_URI
                    "AUDIO" -> MediaStore.Audio.Media.EXTERNAL_CONTENT_URI
                    else -> MediaStore.Images.Media.EXTERNAL_CONTENT_URI
                }
                lookupPermanentMediaStoreEntry(targetStoreUri, displayName, sizeBytes, modality != "IMAGE")
                    ?: run {
                        runCatching {
                            context.contentResolver.takePersistableUriPermission(
                                uri,
                                Intent.FLAG_GRANT_READ_URI_PERMISSION
                            )
                        }
                        DeviceMediaAsset(
                            mediaPath = uri.toString(),
                            mediaUri = uri.toString(),
                            modality = modality,
                            displayName = displayName.ifBlank { uri.lastPathSegment ?: "$modality-item" },
                            folderLabel = "Picked",
                            modifiedAtSec = if (modifiedSec > 0L) modifiedSec else System.currentTimeMillis() / 1000L,
                            fileSize = sizeBytes,
                            durationMs = durationMs
                        )
                    }
            }

            if (candidateAsset != null && seenKeys.add(candidateAsset.mediaPath)) {
                resolved.add(candidateAsset)
            }
        }
        resolved
    }

    /**
     * Decodes, EXIF-rotates, and downscales an image file or URI to fit within [maxSidePx].
     */
    fun decodeOrientedSampledBitmap(
        filePath: String,
        mediaUri: String,
        context: Context,
        maxSidePx: Int = PHOTO_MAX_SIDE_PX
    ): Bitmap? {
        return try {
            val file = File(filePath)
            val useFile = file.exists() && file.canRead()
            val uri = if (!useFile && mediaUri.isNotBlank()) Uri.parse(mediaUri) else null
            if (!useFile && uri == null) return null

            val boundsOpts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            if (useFile) {
                BitmapFactory.decodeFile(file.absolutePath, boundsOpts)
            } else {
                context.contentResolver.openInputStream(uri!!)?.use {
                    BitmapFactory.decodeStream(it, null, boundsOpts)
                }
            }
            if (boundsOpts.outWidth <= 0 || boundsOpts.outHeight <= 0) return null

            var inSample = 1
            val longest = max(boundsOpts.outWidth, boundsOpts.outHeight)
            while ((longest / inSample) > maxSidePx * 2) {
                inSample *= 2
            }

            val decodeOpts = BitmapFactory.Options().apply {
                inSampleSize = inSample
                inPreferredConfig = Bitmap.Config.ARGB_8888
            }
            val rawBmp = if (useFile) {
                BitmapFactory.decodeFile(file.absolutePath, decodeOpts)
            } else {
                context.contentResolver.openInputStream(uri!!)?.use {
                    BitmapFactory.decodeStream(it, null, decodeOpts)
                }
            } ?: return null

            val rotationDeg = try {
                val exif = if (useFile) {
                    ExifInterface(file.absolutePath)
                } else {
                    context.contentResolver.openInputStream(uri!!)?.use { ExifInterface(it) }
                }
                when (exif?.getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)) {
                    ExifInterface.ORIENTATION_ROTATE_90 -> 90f
                    ExifInterface.ORIENTATION_ROTATE_180 -> 180f
                    ExifInterface.ORIENTATION_ROTATE_270 -> 270f
                    else -> 0f
                }
            } catch (_: Throwable) { 0f }

            var working = rawBmp
            if (rotationDeg != 0f) {
                val matrix = Matrix().apply { postRotate(rotationDeg) }
                val rotated = Bitmap.createBitmap(working, 0, 0, working.width, working.height, matrix, true)
                if (rotated !== working) {
                    working.recycle()
                    working = rotated
                }
            }

            val curMax = max(working.width, working.height)
            if (curMax > maxSidePx) {
                val scale = maxSidePx.toFloat() / curMax.toFloat()
                val scaled = Bitmap.createScaledBitmap(
                    working,
                    max(1, (working.width * scale).roundToInt()),
                    max(1, (working.height * scale).roundToInt()),
                    true
                )
                if (scaled !== working) {
                    working.recycle()
                    working = scaled
                }
            }
            working
        } catch (e: Exception) {
            Log.w(TAG, "decodeOrientedSampledBitmap failed ($filePath): ${e.message}")
            null
        }
    }

    fun prepareImageForEmbedding(
        filePath: String,
        mediaUri: String,
        context: Context,
        tmpDir: File,
        maxSidePx: Int = PHOTO_MAX_SIDE_PX
    ): File? {
        val working = decodeOrientedSampledBitmap(filePath, mediaUri, context, maxSidePx) ?: return null
        return try {
            tmpDir.mkdirs()
            val outFile = File(tmpDir, "emb_img_${System.nanoTime()}.jpg")
            FileOutputStream(outFile).use { fos ->
                working.compress(Bitmap.CompressFormat.JPEG, 85, fos)
            }
            outFile
        } catch (e: Exception) {
            Log.w(TAG, "prepareImageForEmbedding compress failed ($filePath): ${e.message}")
            null
        } finally {
            if (!working.isRecycled) working.recycle()
        }
    }

    data class ExtractedAudioSegmentFile(
        val index: Int,
        val startMs: Long,
        val endMs: Long,
        val wavFile: File
    )

    fun extractAudioTrackToWavFiles(
        filePath: String,
        mediaUri: String,
        context: Context,
        tmpDir: File,
        maxWindows: Int = 10
    ): List<ExtractedAudioSegmentFile> {
        val extractor = MediaExtractor()
        var codec: MediaCodec? = null
        try {
            val file = File(filePath)
            if (file.exists() && file.canRead()) {
                extractor.setDataSource(file.absolutePath)
            } else if (mediaUri.isNotBlank()) {
                extractor.setDataSource(context, Uri.parse(mediaUri), null)
            } else {
                return emptyList()
            }

            var audioTrackIndex = -1
            var trackFormat: MediaFormat? = null
            for (i in 0 until extractor.trackCount) {
                val fmt = extractor.getTrackFormat(i)
                val mime = fmt.getString(MediaFormat.KEY_MIME) ?: ""
                if (mime.startsWith("audio/")) {
                    audioTrackIndex = i
                    trackFormat = fmt
                    break
                }
            }
            if (audioTrackIndex < 0 || trackFormat == null) return emptyList()

            extractor.selectTrack(audioTrackIndex)
            val mime = trackFormat.getString(MediaFormat.KEY_MIME) ?: return emptyList()
            var sampleRate = trackFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            var channelCount = trackFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)

            codec = MediaCodec.createDecoderByType(mime)
            codec.configure(trackFormat, null, null, 0)
            codec.start()

            val bufferInfo = MediaCodec.BufferInfo()
            val decodedBytes = ByteArrayOutputStream()
            // Cap decoded raw PCM to ~185s of 48kHz stereo 16-bit (~35MB raw, or ~10MB after resampling guard)
            val maxRawBytes = max(sampleRate * channelCount * 2 * 185, 16000 * 2 * 185)
            var sawInputEOS = false
            var sawOutputEOS = false
            val maxDecodeIterations = 25000
            var iterations = 0

            while (!sawOutputEOS && iterations++ < maxDecodeIterations) {
                if (!sawInputEOS) {
                    val inIdx = codec.dequeueInputBuffer(10_000L)
                    if (inIdx >= 0) {
                        val inBuf = codec.getInputBuffer(inIdx)
                        val sampleSize = if (inBuf != null) extractor.readSampleData(inBuf, 0) else -1
                        if (sampleSize < 0) {
                            codec.queueInputBuffer(inIdx, 0, 0, 0L, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            sawInputEOS = true
                        } else {
                            codec.queueInputBuffer(inIdx, 0, sampleSize, extractor.sampleTime, 0)
                            extractor.advance()
                        }
                    }
                }

                val outIdx = codec.dequeueOutputBuffer(bufferInfo, 10_000L)
                when {
                    outIdx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        val newFmt = codec.outputFormat
                        if (newFmt.containsKey(MediaFormat.KEY_SAMPLE_RATE)) {
                            sampleRate = newFmt.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                        }
                        if (newFmt.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) {
                            channelCount = newFmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                        }
                    }
                    outIdx >= 0 -> {
                        val outBuf = codec.getOutputBuffer(outIdx)
                        if (outBuf != null && bufferInfo.size > 0) {
                            val chunk = ByteArray(bufferInfo.size)
                            outBuf.position(bufferInfo.offset)
                            outBuf.limit(bufferInfo.offset + bufferInfo.size)
                            outBuf.get(chunk)
                            decodedBytes.write(chunk)
                        }
                        codec.releaseOutputBuffer(outIdx, false)
                        if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0 ||
                            decodedBytes.size() >= maxRawBytes
                        ) {
                            sawOutputEOS = true
                        }
                    }
                }
            }

            val rawByteArr = decodedBytes.toByteArray()
            if (rawByteArr.size < 2) return emptyList()
            val totalShorts = rawByteArr.size / 2
            val interleaved = ShortArray(totalShorts)
            ByteBuffer.wrap(rawByteArr, 0, totalShorts * 2).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(interleaved)

            val mono16k = AudioPcmResampler.resampleAndDownmixToMono16k(
                interleavedPcm = interleaved,
                sampleCount = totalShorts,
                sourceSampleRate = sampleRate,
                channelCount = channelCount
            )
            val windows = AudioPcmResampler.sliceAudioWindows(
                mono16k = mono16k,
                windowSec = 20,
                stepSec = 15,
                maxWindows = maxWindows,
                sampleRate = 16000
            )
            if (windows.isEmpty()) return emptyList()

            tmpDir.mkdirs()
            return windows.map { w ->
                val wavBytes = WavEncoder.encodeMono16kWav(w.samples, 16000)
                val outFile = File(tmpDir, "emb_aud_${w.index}_${System.nanoTime()}.wav")
                outFile.writeBytes(wavBytes)
                ExtractedAudioSegmentFile(w.index, w.startMs, w.endMs, outFile)
            }
        } catch (e: Exception) {
            Log.w(TAG, "extractAudioTrackToWavFiles failed ($filePath): ${e.message}")
            return emptyList()
        } finally {
            try { codec?.stop() } catch (_: Throwable) {}
            try { codec?.release() } catch (_: Throwable) {}
            try { extractor.release() } catch (_: Throwable) {}
        }
    }

    data class ExtractedVideoFrameFile(
        val index: Int,
        val timestampMs: Long,
        val endMs: Long,
        val jpegFile: File
    )

    fun extractVideoFramesToJpegFiles(
        filePath: String,
        mediaUri: String,
        context: Context,
        tmpDir: File,
        hintedDurationMs: Long = 0L,
        maxSidePx: Int = 512
    ): Pair<Long, List<ExtractedVideoFrameFile>> {
        val retriever = MediaMetadataRetriever()
        try {
            val file = File(filePath)
            if (file.exists() && file.canRead()) {
                retriever.setDataSource(file.absolutePath)
            } else if (mediaUri.isNotBlank()) {
                retriever.setDataSource(context, Uri.parse(mediaUri))
            } else {
                return hintedDurationMs to emptyList()
            }

            val durStr = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
            val durationMs = durStr?.toLongOrNull()?.takeIf { it > 0L } ?: hintedDurationMs
            val timestamps = MediaFrameScheduler.computeSampleTimestampsMs(durationMs)
            val frames = mutableListOf<ExtractedVideoFrameFile>()
            tmpDir.mkdirs()

            timestamps.forEachIndexed { idx, tsMs ->
                val bmp = retriever.getFrameAtTime(tsMs * 1000L, MediaMetadataRetriever.OPTION_CLOSEST_SYNC)
                    ?: retriever.getFrameAtTime(tsMs * 1000L, MediaMetadataRetriever.OPTION_CLOSEST)
                    ?: return@forEachIndexed
                try {
                    val curMax = max(bmp.width, bmp.height)
                    val finalBmp = if (curMax > maxSidePx) {
                        val scale = maxSidePx.toFloat() / curMax.toFloat()
                        Bitmap.createScaledBitmap(
                            bmp,
                            max(1, (bmp.width * scale).roundToInt()),
                            max(1, (bmp.height * scale).roundToInt()),
                            true
                        )
                    } else {
                        bmp
                    }
                    val outFile = File(tmpDir, "emb_vframe_${idx}_${System.nanoTime()}.jpg")
                    FileOutputStream(outFile).use { fos ->
                        finalBmp.compress(Bitmap.CompressFormat.JPEG, 82, fos)
                    }
                    if (finalBmp !== bmp) finalBmp.recycle()
                    val endMs = timestamps.getOrNull(idx + 1) ?: max(tsMs + 2000L, durationMs)
                    frames.add(ExtractedVideoFrameFile(idx, tsMs, endMs, outFile))
                } finally {
                    if (!bmp.isRecycled) bmp.recycle()
                }
            }
            return durationMs to frames
        } catch (e: Exception) {
            Log.w(TAG, "extractVideoFramesToJpegFiles failed ($filePath): ${e.message}")
            return hintedDurationMs to emptyList()
        } finally {
            try { retriever.release() } catch (_: Throwable) {}
        }
    }

    private fun readGuardrailStatus(context: Context): String? {
        try {
            val batteryIntent = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            val tempC = (batteryIntent?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) ?: 300) / 10f
            if (tempC >= 43.0f) {
                return "Paused for thermal safety (${"%.1f".format(tempC)}°C >= 43°C)"
            }
            val am = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
            if (am != null) {
                val mi = ActivityManager.MemoryInfo()
                am.getMemoryInfo(mi)
                val availGb = mi.availMem / (1024f * 1024f * 1024f)
                if (availGb < 0.9f) {
                    return "Paused due to low RAM (${"%.2f".format(availGb)} GB free)"
                }
            }
        } catch (_: Throwable) {}
        return null
    }

    suspend fun indexPhoneMedia(
        context: Context,
        nativeEngine: NativeEngine,
        scope: MediaIndexScope = MediaIndexScope.ALL,
        maxPerModality: Int = 5000,
        excludeScreenshots: Boolean = false,
        isCancelled: () -> Boolean = { false },
        onProgress: (MediaIndexProgress) -> Unit = {}
    ): MediaIndexSummary = withContext(Dispatchers.IO) {
        val assets = discoverDeviceMedia(
            context = context,
            scope = scope,
            maxPerModality = maxPerModality,
            excludeScreenshots = excludeScreenshots
        )
        indexMediaAssets(
            context = context,
            nativeEngine = nativeEngine,
            assets = assets,
            isCancelled = isCancelled,
            onProgress = onProgress
        )
    }

    suspend fun indexSpecificMediaUris(
        context: Context,
        nativeEngine: NativeEngine,
        uris: List<Uri>,
        isCancelled: () -> Boolean = { false },
        onProgress: (MediaIndexProgress) -> Unit = {}
    ): MediaIndexSummary = withContext(Dispatchers.IO) {
        val assets = resolvePickedUrisToAssets(context, uris)
        indexMediaAssets(
            context = context,
            nativeEngine = nativeEngine,
            assets = assets,
            isCancelled = isCancelled,
            onProgress = onProgress
        )
    }

    suspend fun indexMediaAssets(
        context: Context,
        nativeEngine: NativeEngine,
        assets: List<DeviceMediaAsset>,
        isCancelled: () -> Boolean = { false },
        onProgress: (MediaIndexProgress) -> Unit = {}
    ): MediaIndexSummary = withContext(Dispatchers.IO) {
        val modelPath = nativeEngine.resolveEmbeddingModelPath(requireMultimodal = true)
        if (modelPath.isBlank() || !File(modelPath).exists()) {
            return@withContext MediaIndexSummary(
                success = false,
                modelId = "",
                supportsVision = false,
                supportsAudio = false,
                error = "EmbeddingGemma 2 multimodal model not installed. Please download EmbeddingGemma 2 740M (~462 MB) from Settings > Hugging Face Model Hub."
            )
        }

        val infoJson = nativeEngine.inspectEmbeddingModelAsync(modelPath)
        val modelId = nativeEngine.resolveActiveEmbeddingModelId(modelPath)
        val isEmb = infoJson?.optBoolean("is_embedding", false) ?: false
        val supportsVision = infoJson?.optBoolean("vision", false) ?: false
        val supportsAudio = infoJson?.optBoolean("audio", false) ?: false

        if (!isEmb || (!supportsVision && !supportsAudio)) {
            val installedName = File(modelPath).name
            return@withContext MediaIndexSummary(
                success = false,
                modelId = modelId,
                supportsVision = supportsVision,
                supportsAudio = supportsAudio,
                error = "Active model '$installedName' does not expose Vision/Audio encoders (vision=$supportsVision, audio=$supportsAudio). Please install EmbeddingGemma 2 740M (Omnimodal) in Settings."
            )
        }

        if (assets.isEmpty()) {
            return@withContext MediaIndexSummary(
                success = true,
                modelId = modelId,
                supportsVision = supportsVision,
                supportsAudio = supportsAudio
            )
        }

        val preStats = nativeEngine.getMediaIndexStatsSafe(modelId)
        val upgradeAudioOnlyVideos = supportsVision &&
            preStats.optInt("videos", 0) > 0 &&
            preStats.optInt("video_frames", 0) == 0

        val tmpDir = File(context.cacheDir, "media_emb_tmp").apply { mkdirs() }
        var indexedImages = 0
        var indexedAudio = 0
        var indexedVideos = 0
        var skipped = 0
        var failed = 0
        var cancelled = false
        var stoppedReason: String? = null
        var processedCount = 0
        val totalAssets = assets.size

        val imageAssets = assets.filter { it.modality == "IMAGE" }
        val nonImageAssets = assets.filter { it.modality != "IMAGE" }

        try {
            // 1. Photos: fast pre-filter unchanged items, then mini-batch 4 photos/IPC at 448px & 70 vision tokens
            if (imageAssets.isNotEmpty()) {
                if (!supportsVision) {
                    skipped += imageAssets.size
                    processedCount += imageAssets.size
                } else {
                    val unindexedPhotos = mutableListOf<DeviceMediaAsset>()
                    for (photo in imageAssets) {
                        if (isCancelled()) {
                            cancelled = true
                            break
                        }
                        if (nativeEngine.isMediaIndexedSafe(photo.mediaPath, photo.modifiedAtSec, modelId)) {
                            skipped++
                            processedCount++
                        } else {
                            unindexedPhotos.add(photo)
                        }
                    }

                    val photoBatches = unindexedPhotos.chunked(PHOTO_BATCH_SIZE)
                    for (batch in photoBatches) {
                        if (isCancelled()) {
                            cancelled = true
                            break
                        }
                        val guardrail = readGuardrailStatus(context)
                        if (guardrail != null) {
                            stoppedReason = guardrail
                            break
                        }

                        val leadName = batch.first().displayName
                        onProgress(
                            MediaIndexProgress(
                                currentIndex = processedCount + 1,
                                totalCount = totalAssets,
                                currentFileName = leadName,
                                statusText = if (batch.size > 1) {
                                    "Indexing photos (${processedCount + 1}..${min(totalAssets, processedCount + batch.size)}/$totalAssets): $leadName (+${batch.size - 1})"
                                } else {
                                    "Indexing (${processedCount + 1}/$totalAssets): $leadName"
                                }
                            )
                        )

                        val batchTmpFiles = mutableListOf<File>()
                        try {
                            val prepared = mutableListOf<Pair<DeviceMediaAsset, File>>()
                            for (item in batch) {
                                val imgFile = prepareImageForEmbedding(
                                    filePath = item.mediaPath,
                                    mediaUri = item.mediaUri,
                                    context = context,
                                    tmpDir = tmpDir,
                                    maxSidePx = PHOTO_MAX_SIDE_PX
                                )
                                if (imgFile != null) {
                                    batchTmpFiles.add(imgFile)
                                    prepared.add(item to imgFile)
                                } else {
                                    failed++
                                }
                            }

                            if (prepared.isNotEmpty()) {
                                val vecs = nativeEngine.generateMultimodalEmbeddingsBatchSafe(
                                    items = prepared.map { NativeEngine.MultimodalEmbeddingInput("IMAGE_FILE", it.second.absolutePath) },
                                    visionTokensPerImage = PHOTO_VISION_TOKENS,
                                    customEmbPath = modelPath
                                )

                                fun storePhotoEmbedding(asset: DeviceMediaAsset, vec: FloatArray): Boolean {
                                    return nativeEngine.storeMediaEmbeddingsAtomicSafe(
                                        mediaPath = asset.mediaPath,
                                        mediaUri = asset.mediaUri,
                                        displayName = asset.displayName,
                                        metadataText = "${asset.displayName} ${asset.folderLabel}".trim(),
                                        modifiedAt = asset.modifiedAtSec,
                                        fileSize = asset.fileSize,
                                        durationMs = asset.durationMs,
                                        modelId = modelId,
                                        modalities = listOf("image"),
                                        segmentIndices = listOf(0),
                                        startMsList = listOf(0L),
                                        endMsList = listOf(0L),
                                        embeddings = listOf(vec),
                                        assetTypes = listOf("image")
                                    )
                                }

                                if (vecs.size == prepared.size) {
                                    prepared.forEachIndexed { pIdx, (asset, _) ->
                                        if (storePhotoEmbedding(asset, vecs[pIdx])) indexedImages++ else failed++
                                    }
                                } else {
                                    // Resilient fallback: retry each prepared photo individually if batch failed on one corrupt image
                                    for ((asset, imgFile) in prepared) {
                                        if (isCancelled()) {
                                            cancelled = true
                                            break
                                        }
                                        val singleVecs = nativeEngine.generateMultimodalEmbeddingsBatchSafe(
                                            items = listOf(NativeEngine.MultimodalEmbeddingInput("IMAGE_FILE", imgFile.absolutePath)),
                                            visionTokensPerImage = PHOTO_VISION_TOKENS,
                                            customEmbPath = modelPath
                                        )
                                        val sVec = singleVecs.firstOrNull()
                                        if (sVec != null && storePhotoEmbedding(asset, sVec)) {
                                            indexedImages++
                                        } else {
                                            failed++
                                        }
                                    }
                                }
                            }
                        } finally {
                            batchTmpFiles.forEach { runCatching { it.delete() } }
                        }

                        processedCount += batch.size
                        yield()
                    }
                }
            }

            // 2. Audio & Video assets processed sequentially (multi-segment per file)
            for (asset in nonImageAssets) {
                if (cancelled || stoppedReason != null) break
                if (isCancelled()) {
                    cancelled = true
                    break
                }
                val guardrail = readGuardrailStatus(context)
                if (guardrail != null) {
                    stoppedReason = guardrail
                    break
                }

                processedCount++
                onProgress(
                    MediaIndexProgress(
                        currentIndex = processedCount,
                        totalCount = totalAssets,
                        currentFileName = asset.displayName,
                        statusText = "Indexing ($processedCount/$totalAssets): ${asset.displayName}"
                    )
                )

                val shouldSkipExisting = !(upgradeAudioOnlyVideos && asset.modality == "VIDEO") &&
                    nativeEngine.isMediaIndexedSafe(asset.mediaPath, asset.modifiedAtSec, modelId)
                if (shouldSkipExisting) {
                    skipped++
                    continue
                }

                val tempFilesToDelete = mutableListOf<File>()
                try {
                    val segModalities = mutableListOf<String>()
                    val segAssetTypes = mutableListOf<String>()
                    val segIndices = mutableListOf<Int>()
                    val segStarts = mutableListOf<Long>()
                    val segEnds = mutableListOf<Long>()
                    val segEmbeddings = mutableListOf<FloatArray>()
                    var resolvedDurationMs = asset.durationMs

                    when (asset.modality) {
                        "AUDIO" -> {
                            if (!supportsAudio) {
                                skipped++
                                continue
                            }
                            val audioSegs = extractAudioTrackToWavFiles(asset.mediaPath, asset.mediaUri, context, tmpDir, maxWindows = 10)
                            audioSegs.forEach { tempFilesToDelete.add(it.wavFile) }
                            if (audioSegs.isNotEmpty()) {
                                val vecs = nativeEngine.generateMultimodalEmbeddingsBatchSafe(
                                    items = audioSegs.map { NativeEngine.MultimodalEmbeddingInput("AUDIO_FILE", it.wavFile.absolutePath) },
                                    visionTokensPerImage = PHOTO_VISION_TOKENS,
                                    customEmbPath = modelPath
                                )
                                if (vecs.size == audioSegs.size) {
                                    audioSegs.forEachIndexed { sIdx, seg ->
                                        segModalities.add("audio")
                                        segAssetTypes.add("audio")
                                        segIndices.add(seg.index)
                                        segStarts.add(seg.startMs)
                                        segEnds.add(seg.endMs)
                                        segEmbeddings.add(vecs[sIdx])
                                    }
                                    if (resolvedDurationMs <= 0L) {
                                        resolvedDurationMs = audioSegs.maxOf { it.endMs }
                                    }
                                }
                            }
                        }
                        "VIDEO" -> {
                            if (supportsVision) {
                                val (durMs, frameSegs) = extractVideoFramesToJpegFiles(
                                    asset.mediaPath,
                                    asset.mediaUri,
                                    context,
                                    tmpDir,
                                    hintedDurationMs = asset.durationMs,
                                    maxSidePx = VIDEO_FRAME_MAX_SIDE_PX
                                )
                                if (durMs > resolvedDurationMs) resolvedDurationMs = durMs
                                frameSegs.forEach { tempFilesToDelete.add(it.jpegFile) }
                                if (frameSegs.isNotEmpty()) {
                                    val fVecs = nativeEngine.generateMultimodalEmbeddingsBatchSafe(
                                        items = frameSegs.map { NativeEngine.MultimodalEmbeddingInput("IMAGE_FILE", it.jpegFile.absolutePath) },
                                        visionTokensPerImage = VIDEO_FRAME_VISION_TOKENS,
                                        customEmbPath = modelPath
                                    )
                                    if (fVecs.size == frameSegs.size) {
                                        frameSegs.forEachIndexed { fIdx, frame ->
                                            segModalities.add("image")
                                            segAssetTypes.add("video")
                                            segIndices.add(frame.index)
                                            segStarts.add(frame.timestampMs)
                                            segEnds.add(frame.endMs)
                                            segEmbeddings.add(fVecs[fIdx])
                                        }
                                    }
                                }
                            }
                            if (supportsAudio && !isCancelled()) {
                                val vAudioSegs = extractAudioTrackToWavFiles(asset.mediaPath, asset.mediaUri, context, tmpDir, maxWindows = 8)
                                vAudioSegs.forEach { tempFilesToDelete.add(it.wavFile) }
                                if (vAudioSegs.isNotEmpty()) {
                                    val aVecs = nativeEngine.generateMultimodalEmbeddingsBatchSafe(
                                        items = vAudioSegs.map { NativeEngine.MultimodalEmbeddingInput("AUDIO_FILE", it.wavFile.absolutePath) },
                                        visionTokensPerImage = VIDEO_FRAME_VISION_TOKENS,
                                        customEmbPath = modelPath
                                    )
                                    if (aVecs.size == vAudioSegs.size) {
                                        vAudioSegs.forEachIndexed { aIdx, aSeg ->
                                            segModalities.add("audio")
                                            segAssetTypes.add("video")
                                            segIndices.add(aSeg.index)
                                            segStarts.add(aSeg.startMs)
                                            segEnds.add(aSeg.endMs)
                                            segEmbeddings.add(aVecs[aIdx])
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (segEmbeddings.isNotEmpty()) {
                        val stored = nativeEngine.storeMediaEmbeddingsAtomicSafe(
                            mediaPath = asset.mediaPath,
                            mediaUri = asset.mediaUri,
                            displayName = asset.displayName,
                            metadataText = "${asset.displayName} ${asset.folderLabel}".trim(),
                            modifiedAt = asset.modifiedAtSec,
                            fileSize = asset.fileSize,
                            durationMs = resolvedDurationMs,
                            modelId = modelId,
                            modalities = segModalities,
                            segmentIndices = segIndices,
                            startMsList = segStarts,
                            endMsList = segEnds,
                            embeddings = segEmbeddings,
                            assetTypes = segAssetTypes
                        )
                        if (stored) {
                            when (asset.modality) {
                                "AUDIO" -> indexedAudio++
                                "VIDEO" -> indexedVideos++
                            }
                        } else {
                            failed++
                        }
                    } else {
                        failed++
                    }
                } finally {
                    tempFilesToDelete.forEach { runCatching { it.delete() } }
                }
                yield()
            }
        } finally {
            nativeEngine.releaseEmbeddingEngineSafe()
            runCatching { tmpDir.deleteRecursively() }
        }

        MediaIndexSummary(
            success = true,
            modelId = modelId,
            supportsVision = supportsVision,
            supportsAudio = supportsAudio,
            indexedImages = indexedImages,
            indexedAudio = indexedAudio,
            indexedVideos = indexedVideos,
            skippedUnchanged = skipped,
            failedCount = failed,
            cancelled = cancelled,
            stoppedForGuardrail = stoppedReason
        )
    }

    fun parseMediaSearchMatchJson(j: JSONObject): MediaSearchResultItem {
        val cosine = j.optDouble("cosine_score", 0.0).toFloat()
        val finalScore = j.optDouble("final_score", cosine.toDouble()).toFloat()
        return MediaSearchResultItem(
            mediaPath = j.optString("media_path", ""),
            mediaUri = j.optString("media_uri", ""),
            modality = j.optString("modality", ""),
            displayName = j.optString("display_name", ""),
            startMs = j.optLong("start_ms", 0L),
            endMs = j.optLong("end_ms", 0L),
            durationMs = j.optLong("duration_ms", 0L),
            score = finalScore,
            modelId = j.optString("model_id", ""),
            cosineScore = cosine,
            zScore = j.optDouble("z_score", 0.0).toFloat(),
            corpusMean = j.optDouble("corpus_mean", 0.0).toFloat(),
            corpusStd = j.optDouble("corpus_std", 0.0).toFloat(),
            corpusSize = j.optInt("corpus_size", 0),
            lexicalHits = j.optInt("lexical_hits", 0),
            assetType = j.optString("asset_type", "")
        )
    }

    fun listIndexedMedia(
        nativeEngine: NativeEngine,
        filter: MediaModalityFilter = MediaModalityFilter.ALL,
        modelId: String = "",
        limit: Int = 500
    ): List<IndexedMediaEntry> {
        return nativeEngine.listIndexedMediaSafe(
            modalityFilter = filter.sqlFilter,
            modelId = modelId,
            limit = limit
        ).map { j ->
            IndexedMediaEntry(
                mediaPath = j.optString("media_path", ""),
                mediaUri = j.optString("media_uri", ""),
                modality = j.optString("modality", ""),
                displayName = j.optString("display_name", ""),
                durationMs = j.optLong("duration_ms", 0L),
                modifiedAtSec = j.optLong("modified_at", 0L),
                segmentCount = j.optInt("segment_count", 1),
                lastIndexedSec = j.optLong("last_indexed", 0L),
                modelId = j.optString("model_id", "")
            )
        }
    }

    suspend fun searchMediaByText(
        nativeEngine: NativeEngine,
        query: String,
        filter: MediaModalityFilter = MediaModalityFilter.ALL,
        topK: Int = 24
    ): MediaSearchOutcome = withContext(Dispatchers.IO) {
        val modelPath = nativeEngine.resolveEmbeddingModelPath(requireMultimodal = true)
        val modelId = if (modelPath.isNotBlank()) nativeEngine.resolveActiveEmbeddingModelId(modelPath) else ""
        val stats = nativeEngine.getMediaIndexStatsSafe(modelId)
        val totalSegs = stats.optInt("total_segments", 0)
        if (totalSegs == 0) {
            val anyStats = nativeEngine.getMediaIndexStatsSafe("")
            if (anyStats.optInt("total_segments", 0) == 0) {
                return@withContext MediaSearchOutcome(
                    success = true,
                    query = query,
                    filter = filter,
                    modelId = modelId,
                    results = emptyList(),
                    indexEmpty = true
                )
            }
        }

        val effectiveModelId = if (totalSegs > 0) modelId else ""
        val effectiveFilter = if (filter == MediaModalityFilter.VISUAL) MediaModalityFilter.IMAGE else filter
        val qVecs = if (modelPath.isNotBlank() && File(modelPath).exists()) {
            nativeEngine.generateEmbeddingsSubBatched(
                texts = listOf(query),
                customEmbPath = modelPath,
                taskMode = InferenceService.TASK_QUERY
            )
        } else {
            emptyList()
        }
        val queryVec = qVecs.firstOrNull() ?: FloatArray(0)
        val rawJson = nativeEngine.searchMediaEmbeddingsSafe(
            queryEmbedding = queryVec,
            modalityFilter = effectiveFilter.sqlFilter,
            modelId = effectiveModelId,
            lexicalQuery = query,
            topK = max(topK * 3, 40)
        )
        val parsed = rawJson.map(::parseMediaSearchMatchJson)
        val scannedCount = parsed.firstOrNull()?.corpusSize ?: parsed.size
        val bestRawCosine = parsed.maxOfOrNull { it.cosineScore } ?: 0f
        val relevant = filterRelevantMediaMatches(parsed, MediaSearchQueryMode.TEXT_TO_MEDIA)
        val isolated = when (effectiveFilter) {
            MediaModalityFilter.IMAGE -> enforceStrictModalityIsolation(relevant, "image", "image")
            MediaModalityFilter.AUDIO -> enforceStrictModalityIsolation(relevant, "audio", "audio")
            MediaModalityFilter.VIDEO -> relevant.filter {
                it.assetType.equals("video", ignoreCase = true) ||
                    it.modality.startsWith("VIDEO", ignoreCase = true)
            }
            else -> relevant
        }
        val ranked = deduplicateAndRankMediaMatches(isolated, topK = topK)
        MediaSearchOutcome(
            success = true,
            query = query,
            filter = effectiveFilter,
            modelId = effectiveModelId,
            results = ranked,
            indexEmpty = false,
            scannedCount = scannedCount,
            bestRejectedScore = if (ranked.isEmpty()) bestRawCosine else 0f
        )
    }

    suspend fun searchMediaByImage(
        context: Context,
        nativeEngine: NativeEngine,
        imageFile: File,
        filter: MediaModalityFilter = MediaModalityFilter.IMAGE,
        topK: Int = 24
    ): MediaSearchOutcome = withContext(Dispatchers.IO) {
        val effectiveFilter = if (filter == MediaModalityFilter.VISUAL || filter == MediaModalityFilter.ALL) {
            MediaModalityFilter.IMAGE
        } else {
            filter
        }
        val modelPath = nativeEngine.resolveEmbeddingModelPath(requireMultimodal = true)
        if (modelPath.isBlank() || !File(modelPath).exists()) {
            return@withContext MediaSearchOutcome(
                success = false,
                query = imageFile.name,
                filter = effectiveFilter,
                error = "EmbeddingGemma 2 multimodal model not installed. Please download EmbeddingGemma 2 740M (~462 MB) from Settings > Hugging Face Model Hub (your Gemma 4 Chat Brain stays active)."
            )
        }
        val embFileName = File(modelPath).name
        val modelId = nativeEngine.resolveActiveEmbeddingModelId(modelPath)
        val infoJson = nativeEngine.inspectEmbeddingModelAsync(modelPath)
            ?: return@withContext MediaSearchOutcome(
                success = false,
                query = imageFile.name,
                filter = effectiveFilter,
                modelId = modelId,
                error = "Inference worker service unavailable while inspecting $embFileName. Please retry."
            )
        val isEmb = infoJson.optBoolean("is_embedding", false)
        val supportsVision = infoJson.optBoolean("vision", false)
        if (!isEmb || !supportsVision) {
            val supportsText = infoJson.optBoolean("text", false)
            val supportsAudio = infoJson.optBoolean("audio", false)
            return@withContext MediaSearchOutcome(
                success = false,
                query = imageFile.name,
                filter = effectiveFilter,
                modelId = modelId,
                error = "Embedding model $embFileName does not support Vision inputs (text=$supportsText, vision=$supportsVision, audio=$supportsAudio).\n\nVisual Search အတွက် Settings > Hugging Face Model Hub မှ EmbeddingGemma 2 740M (Omnimodal) (~462 MB) ကို download လုပ်ပေးပါ (Active Brain က Gemma 4 ပဲ ဆက်ရှိနေမှာဖြစ်ပြီး EmbeddingGemma 2 က On-Demand အလိုအလျောက် တွဲအလုပ်လုပ်ပါမည်)။"
            )
        }

        val modelStats = nativeEngine.getMediaIndexStatsSafe(modelId)
        val relevantSegments = when (effectiveFilter) {
            MediaModalityFilter.IMAGE -> modelStats.optInt("images", 0)
            MediaModalityFilter.AUDIO -> modelStats.optInt("audio", 0)
            MediaModalityFilter.VIDEO -> modelStats.optInt("videos", 0)
            else -> modelStats.optInt("images", 0)
        }
        if (relevantSegments == 0) {
            return@withContext MediaSearchOutcome(
                success = true,
                query = imageFile.name,
                filter = effectiveFilter,
                modelId = modelId,
                indexEmpty = true
            )
        }

        val tmpDir = File(context.cacheDir, "media_emb_query_tmp").apply { mkdirs() }
        val prepImg = prepareImageForEmbedding(imageFile.absolutePath, "", context, tmpDir, maxSidePx = PHOTO_MAX_SIDE_PX)
        if (prepImg == null) {
            return@withContext MediaSearchOutcome(
                success = false,
                query = imageFile.name,
                filter = effectiveFilter,
                error = "Could not decode reference image ${imageFile.name}."
            )
        }

        return@withContext try {
            val vecs = nativeEngine.generateMultimodalEmbeddingsBatchSafe(
                items = listOf(NativeEngine.MultimodalEmbeddingInput("IMAGE_FILE", prepImg.absolutePath)),
                visionTokensPerImage = PHOTO_VISION_TOKENS,
                customEmbPath = modelPath
            )
            val qVec = vecs.firstOrNull()
                ?: run {
                    val workerDetail = nativeEngine.getLastEmbeddingErrorSafe()
                    val reasonSuffix = if (workerDetail.isNotBlank()) "\n• Detail: $workerDetail" else ""
                    return@withContext MediaSearchOutcome(
                        success = false,
                        query = imageFile.name,
                        filter = effectiveFilter,
                        modelId = modelId,
                        error = "Vision encoder failed on $embFileName while encoding ${imageFile.name}.$reasonSuffix"
                    )
                }

            val rawJson = nativeEngine.searchMediaEmbeddingsSafe(
                queryEmbedding = qVec,
                modalityFilter = effectiveFilter.sqlFilter,
                modelId = modelId,
                lexicalQuery = "",
                topK = max(topK * 3, 40)
            )
            val parsed = rawJson.map(::parseMediaSearchMatchJson)
            val scannedCount = parsed.firstOrNull()?.corpusSize ?: parsed.size
            val bestRawCosine = parsed.maxOfOrNull { it.cosineScore } ?: 0f
            val relevant = filterRelevantMediaMatches(parsed, MediaSearchQueryMode.IMAGE_TO_VISUAL)
            val isolated = enforceStrictModalityIsolation(relevant, allowedModality = "image", allowedAssetType = "image")
            val ranked = deduplicateAndRankMediaMatches(isolated, topK = topK)
            MediaSearchOutcome(
                success = true,
                query = "Similar to ${imageFile.name}",
                filter = effectiveFilter,
                modelId = modelId,
                results = ranked,
                indexEmpty = false,
                scannedCount = scannedCount,
                bestRejectedScore = if (ranked.isEmpty()) bestRawCosine else 0f
            )
        } finally {
            runCatching { prepImg.delete() }
            runCatching { tmpDir.deleteRecursively() }
        }
    }
}
