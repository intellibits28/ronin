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
    IMAGE("IMAGE", "Photos"),
    AUDIO("AUDIO", "Audio"),
    VIDEO("VIDEO", "Videos")
}

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
    val modality: String, // "IMAGE", "AUDIO", "VIDEO_FRAME", "VIDEO_AUDIO"
    val displayName: String,
    val startMs: Long,
    val endMs: Long,
    val durationMs: Long,
    val score: Float,
    val extraMomentsMs: List<Long> = emptyList(),
    val modelId: String = ""
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
    val error: String? = null
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

    suspend fun discoverDeviceMedia(
        context: Context,
        scope: MediaIndexScope = MediaIndexScope.ALL,
        maxPerModality: Int = 60
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
                        if (key.isBlank() || !seenPaths.add(key)) continue

                        val name = (if (nameCol >= 0) cursor.getString(nameCol) else null)
                            ?: File(path).name.ifBlank { "$modality-$id" }
                        val modSec = if (modCol >= 0) cursor.getLong(modCol) else 0L
                        val fileSize = if (sizeCol >= 0) cursor.getLong(sizeCol) else 0L
                        val durationMs = if (durCol >= 0) cursor.getLong(durCol).coerceAtLeast(0L) else 0L

                        // Filter out tiny UI icons or zero-byte artifacts
                        if (fileSize in 1L..2048L) continue
                        val folderLabel = File(path).parentFile?.name ?: ""
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
                        if (!f.isFile || f.length() <= 2048L) return@forEach
                        val ext = f.extension.lowercase()
                        val modality = when {
                            ext in imgExts && (scope == MediaIndexScope.ALL || scope == MediaIndexScope.PHOTOS) -> "IMAGE"
                            ext in audExts && (scope == MediaIndexScope.ALL || scope == MediaIndexScope.AUDIO) -> "AUDIO"
                            ext in vidExts && (scope == MediaIndexScope.ALL || scope == MediaIndexScope.VIDEO) -> "VIDEO"
                            else -> return@forEach
                        }
                        if (assets.count { it.modality == modality } >= maxPerModality) return@forEach
                        if (seenPaths.add(f.absolutePath)) {
                            assets.add(
                                DeviceMediaAsset(
                                    mediaPath = f.absolutePath,
                                    mediaUri = Uri.fromFile(f).toString(),
                                    modality = modality,
                                    displayName = f.name,
                                    folderLabel = f.parentFile?.name ?: "",
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

        assets
    }

    fun prepareImageForEmbedding(
        filePath: String,
        mediaUri: String,
        context: Context,
        tmpDir: File,
        maxSidePx: Int = 768
    ): File? {
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

            tmpDir.mkdirs()
            val outFile = File(tmpDir, "emb_img_${System.nanoTime()}.jpg")
            FileOutputStream(outFile).use { fos ->
                working.compress(Bitmap.CompressFormat.JPEG, 85, fos)
            }
            working.recycle()
            outFile
        } catch (e: Exception) {
            Log.w(TAG, "prepareImageForEmbedding failed ($filePath): ${e.message}")
            null
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
        maxPerModality: Int = 60,
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

        val assets = discoverDeviceMedia(context, scope, maxPerModality)
        if (assets.isEmpty()) {
            return@withContext MediaIndexSummary(
                success = true,
                modelId = modelId,
                supportsVision = supportsVision,
                supportsAudio = supportsAudio
            )
        }

        val tmpDir = File(context.cacheDir, "media_emb_tmp").apply { mkdirs() }
        var indexedImages = 0
        var indexedAudio = 0
        var indexedVideos = 0
        var skipped = 0
        var failed = 0
        var cancelled = false
        var stoppedReason: String? = null

        try {
            assets.forEachIndexed { idx, asset ->
                if (isCancelled()) {
                    cancelled = true
                    return@forEachIndexed
                }
                val guardrail = readGuardrailStatus(context)
                if (guardrail != null) {
                    stoppedReason = guardrail
                    return@forEachIndexed
                }

                onProgress(
                    MediaIndexProgress(
                        currentIndex = idx + 1,
                        totalCount = assets.size,
                        currentFileName = asset.displayName,
                        statusText = "Indexing (${idx + 1}/${assets.size}): ${asset.displayName}"
                    )
                )

                if (nativeEngine.isMediaIndexedSafe(asset.mediaPath, asset.modifiedAtSec, modelId)) {
                    skipped++
                    return@forEachIndexed
                }

                val tempFilesToDelete = mutableListOf<File>()
                try {
                    val segModalities = mutableListOf<String>()
                    val segIndices = mutableListOf<Int>()
                    val segStarts = mutableListOf<Long>()
                    val segEnds = mutableListOf<Long>()
                    val segEmbeddings = mutableListOf<FloatArray>()
                    var resolvedDurationMs = asset.durationMs

                    when (asset.modality) {
                        "IMAGE" -> {
                            if (!supportsVision) {
                                skipped++
                                return@forEachIndexed
                            }
                            val imgFile = prepareImageForEmbedding(asset.mediaPath, asset.mediaUri, context, tmpDir, maxSidePx = 768)
                            if (imgFile != null) {
                                tempFilesToDelete.add(imgFile)
                                val vecs = nativeEngine.generateMultimodalEmbeddingsBatchSafe(
                                    items = listOf(NativeEngine.MultimodalEmbeddingInput("IMAGE_FILE", imgFile.absolutePath)),
                                    visionTokensPerImage = 280,
                                    customEmbPath = modelPath
                                )
                                if (vecs.size == 1) {
                                    segModalities.add("IMAGE")
                                    segIndices.add(0)
                                    segStarts.add(0L)
                                    segEnds.add(0L)
                                    segEmbeddings.add(vecs[0])
                                }
                            }
                        }
                        "AUDIO" -> {
                            if (!supportsAudio) {
                                skipped++
                                return@forEachIndexed
                            }
                            val audioSegs = extractAudioTrackToWavFiles(asset.mediaPath, asset.mediaUri, context, tmpDir, maxWindows = 10)
                            audioSegs.forEach { tempFilesToDelete.add(it.wavFile) }
                            if (audioSegs.isNotEmpty()) {
                                val vecs = nativeEngine.generateMultimodalEmbeddingsBatchSafe(
                                    items = audioSegs.map { NativeEngine.MultimodalEmbeddingInput("AUDIO_FILE", it.wavFile.absolutePath) },
                                    visionTokensPerImage = 280,
                                    customEmbPath = modelPath
                                )
                                if (vecs.size == audioSegs.size) {
                                    audioSegs.forEachIndexed { sIdx, seg ->
                                        segModalities.add("AUDIO")
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
                                    maxSidePx = 512
                                )
                                if (durMs > resolvedDurationMs) resolvedDurationMs = durMs
                                frameSegs.forEach { tempFilesToDelete.add(it.jpegFile) }
                                if (frameSegs.isNotEmpty()) {
                                    val fVecs = nativeEngine.generateMultimodalEmbeddingsBatchSafe(
                                        items = frameSegs.map { NativeEngine.MultimodalEmbeddingInput("IMAGE_FILE", it.jpegFile.absolutePath) },
                                        visionTokensPerImage = 140,
                                        customEmbPath = modelPath
                                    )
                                    if (fVecs.size == frameSegs.size) {
                                        frameSegs.forEachIndexed { fIdx, frame ->
                                            segModalities.add("VIDEO_FRAME")
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
                                        visionTokensPerImage = 140,
                                        customEmbPath = modelPath
                                    )
                                    if (aVecs.size == vAudioSegs.size) {
                                        vAudioSegs.forEachIndexed { aIdx, aSeg ->
                                            segModalities.add("VIDEO_AUDIO")
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
                            embeddings = segEmbeddings
                        )
                        if (stored) {
                            when (asset.modality) {
                                "IMAGE" -> indexedImages++
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

    suspend fun searchMediaByText(
        nativeEngine: NativeEngine,
        query: String,
        filter: MediaModalityFilter = MediaModalityFilter.ALL,
        topK: Int = 8
    ): MediaSearchOutcome = withContext(Dispatchers.IO) {
        val modelPath = nativeEngine.resolveEmbeddingModelPath(requireMultimodal = true)
        val modelId = if (modelPath.isNotBlank()) nativeEngine.resolveActiveEmbeddingModelId(modelPath) else ""
        val stats = nativeEngine.getMediaIndexStatsSafe(modelId)
        val totalSegs = stats.optInt("total_segments", 0)
        if (totalSegs == 0) {
            // Check global stats across any modelId before reporting index empty
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

        val effectiveModelId = if (stats.optInt("total_segments", 0) > 0) modelId else ""
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
            modalityFilter = filter.sqlFilter,
            modelId = effectiveModelId,
            lexicalQuery = query,
            topK = topK * 3
        )
        val parsed = rawJson.map { j ->
            MediaSearchResultItem(
                mediaPath = j.optString("media_path", ""),
                mediaUri = j.optString("media_uri", ""),
                modality = j.optString("modality", ""),
                displayName = j.optString("display_name", ""),
                startMs = j.optLong("start_ms", 0L),
                endMs = j.optLong("end_ms", 0L),
                durationMs = j.optLong("duration_ms", 0L),
                score = j.optDouble("final_score", j.optDouble("cosine_score", 0.0)).toFloat(),
                modelId = j.optString("model_id", "")
            )
        }
        MediaSearchOutcome(
            success = true,
            query = query,
            filter = filter,
            modelId = effectiveModelId,
            results = deduplicateAndRankMediaMatches(parsed, topK = topK),
            indexEmpty = false
        )
    }

    suspend fun searchMediaByImage(
        context: Context,
        nativeEngine: NativeEngine,
        imageFile: File,
        filter: MediaModalityFilter = MediaModalityFilter.ALL,
        topK: Int = 8
    ): MediaSearchOutcome = withContext(Dispatchers.IO) {
        val modelPath = nativeEngine.resolveEmbeddingModelPath(requireMultimodal = true)
        if (modelPath.isBlank() || !File(modelPath).exists()) {
            return@withContext MediaSearchOutcome(
                success = false,
                query = imageFile.name,
                filter = filter,
                error = "EmbeddingGemma 2 multimodal model not installed. Please download EmbeddingGemma 2 740M (~462 MB) from Settings > Hugging Face Model Hub (your Gemma 4 Chat Brain stays active)."
            )
        }
        val embFileName = File(modelPath).name
        val modelId = nativeEngine.resolveActiveEmbeddingModelId(modelPath)
        val infoJson = nativeEngine.inspectEmbeddingModelAsync(modelPath)
            ?: return@withContext MediaSearchOutcome(
                success = false,
                query = imageFile.name,
                filter = filter,
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
                filter = filter,
                modelId = modelId,
                error = "Embedding model `$embFileName` does not support Vision inputs (text=$supportsText, vision=$supportsVision, audio=$supportsAudio).\n\nVisual Search အတွက် **Settings > Hugging Face Model Hub** မှ `EmbeddingGemma 2 740M (Omnimodal)` (~462 MB) ကို download လုပ်ပေးပါ (Active Brain က Gemma 4 ပဲ ဆက်ရှိနေမှာဖြစ်ပြီး EmbeddingGemma 2 က On-Demand အလိုအလျောက် တွဲအလုပ်လုပ်ပါမည်)။"
            )
        }

        val anyStats = nativeEngine.getMediaIndexStatsSafe("")
        if (anyStats.optInt("total_segments", 0) == 0) {
            return@withContext MediaSearchOutcome(
                success = true,
                query = imageFile.name,
                filter = filter,
                modelId = modelId,
                indexEmpty = true
            )
        }

        val tmpDir = File(context.cacheDir, "media_emb_query_tmp").apply { mkdirs() }
        val prepImg = prepareImageForEmbedding(imageFile.absolutePath, "", context, tmpDir, maxSidePx = 768)
        if (prepImg == null) {
            return@withContext MediaSearchOutcome(
                success = false,
                query = imageFile.name,
                filter = filter,
                error = "Could not decode reference image ${imageFile.name}."
            )
        }

        return@withContext try {
            val vecs = nativeEngine.generateMultimodalEmbeddingsBatchSafe(
                items = listOf(NativeEngine.MultimodalEmbeddingInput("IMAGE_FILE", prepImg.absolutePath)),
                visionTokensPerImage = 280,
                customEmbPath = modelPath
            )
            val qVec = vecs.firstOrNull()
                ?: run {
                    val workerDetail = nativeEngine.getLastEmbeddingErrorSafe()
                    val reasonSuffix = if (workerDetail.isNotBlank()) "\n• Detail: `$workerDetail`" else ""
                    return@withContext MediaSearchOutcome(
                        success = false,
                        query = imageFile.name,
                        filter = filter,
                        modelId = modelId,
                        error = "Vision encoder failed on `$embFileName` while encoding `${imageFile.name}`.$reasonSuffix"
                    )
                }

            val rawJson = nativeEngine.searchMediaEmbeddingsSafe(
                queryEmbedding = qVec,
                modalityFilter = filter.sqlFilter,
                modelId = "",
                lexicalQuery = "",
                topK = topK * 3
            )
            val parsed = rawJson.map { j ->
                MediaSearchResultItem(
                    mediaPath = j.optString("media_path", ""),
                    mediaUri = j.optString("media_uri", ""),
                    modality = j.optString("modality", ""),
                    displayName = j.optString("display_name", ""),
                    startMs = j.optLong("start_ms", 0L),
                    endMs = j.optLong("end_ms", 0L),
                    durationMs = j.optLong("duration_ms", 0L),
                    score = j.optDouble("final_score", j.optDouble("cosine_score", 0.0)).toFloat(),
                    modelId = j.optString("model_id", "")
                )
            }
            MediaSearchOutcome(
                success = true,
                query = "Similar to ${imageFile.name}",
                filter = filter,
                modelId = modelId,
                results = deduplicateAndRankMediaMatches(parsed, topK = topK),
                indexEmpty = false
            )
        } finally {
            runCatching { prepImg.delete() }
            runCatching { tmpDir.deleteRecursively() }
        }
    }
}
