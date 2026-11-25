package com.ronin.kernel

import android.media.ExifInterface
import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

class MultimodalMediaSearchTest {

    @Test
    fun `formatForEmbeddingGemma2 applies official asymmetric prefixes for queries and documents`() {
        assertEquals(
            "task: search result | query: dog running on beach",
            InferenceService.formatForEmbeddingGemma2("  dog running on beach ", InferenceService.TASK_QUERY)
        )
        assertEquals(
            "title: contract.pdf | text: Section 4 Liability",
            InferenceService.formatForEmbeddingGemma2("Section 4 Liability", InferenceService.TASK_DOCUMENT, "contract.pdf")
        )
        assertEquals(
            "title: none | text: Untitled paragraph",
            InferenceService.formatForEmbeddingGemma2("Untitled paragraph", InferenceService.TASK_DOCUMENT, "")
        )
        assertEquals(
            "raw passthrough",
            InferenceService.formatForEmbeddingGemma2("  raw passthrough ", InferenceService.TASK_RAW)
        )
        assertEquals(
            "စက္ကူပန်း bougainvillea",
            InferenceService.formatForEmbeddingGemma2("  စက္ကူပန်း bougainvillea  ", InferenceService.TASK_NONE)
        )
    }

    @Test
    fun `WavEncoder writes canonical 44-byte RIFF WAVE 16kHz 16-bit mono PCM header and samples`() {
        val samples = shortArrayOf(0, 1234, -5678, 32767)
        val wav = WavEncoder.encodeMono16kWav(samples, sampleRate = 16000)

        assertEquals(44 + samples.size * 2, wav.size)
        assertEquals("RIFF", String(wav, 0, 4, Charsets.US_ASCII))
        assertEquals("WAVE", String(wav, 8, 4, Charsets.US_ASCII))
        assertEquals("fmt ", String(wav, 12, 4, Charsets.US_ASCII))
        assertEquals("data", String(wav, 36, 4, Charsets.US_ASCII))

        val bb = ByteBuffer.wrap(wav).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals(36 + samples.size * 2, bb.getInt(4))
        assertEquals(16, bb.getInt(16)) // PCM fmt chunk size
        assertEquals(1.toShort(), bb.getShort(20)) // AudioFormat PCM
        assertEquals(1.toShort(), bb.getShort(22)) // Mono
        assertEquals(16000, bb.getInt(24)) // SampleRate
        assertEquals(32000, bb.getInt(28)) // ByteRate
        assertEquals(2.toShort(), bb.getShort(32)) // BlockAlign
        assertEquals(16.toShort(), bb.getShort(34)) // BitsPerSample
        assertEquals(samples.size * 2, bb.getInt(40)) // Data bytes
        assertEquals(1234.toShort(), bb.getShort(46))
        assertEquals((-5678).toShort(), bb.getShort(48))
    }

    @Test
    fun `AudioPcmResampler downmixes stereo 48kHz to mono 16kHz and slices bounded sliding windows`() {
        // 35 seconds of 48kHz stereo alternating +4000 / +2000 -> mono average 3000 -> resampled to 16kHz (~560,000 samples)
        val durationSec = 35
        val srcRate = 48000
        val channels = 2
        val totalFrames = durationSec * srcRate
        val interleaved = ShortArray(totalFrames * channels)
        for (f in 0 until totalFrames) {
            interleaved[f * 2] = 4000
            interleaved[f * 2 + 1] = 2000
        }

        val mono16k = AudioPcmResampler.resampleAndDownmixToMono16k(
            interleavedPcm = interleaved,
            sampleCount = interleaved.size,
            sourceSampleRate = srcRate,
            channelCount = channels
        )
        assertEquals(durationSec * 16000, mono16k.size)
        assertEquals(3000.toShort(), mono16k[100])

        val windows = AudioPcmResampler.sliceAudioWindows(
            mono16k = mono16k,
            windowSec = 20,
            stepSec = 15,
            maxWindows = 10,
            sampleRate = 16000
        )
        // [0..20s], [15..35s] -> 2 windows
        assertEquals(2, windows.size)
        assertEquals(0L, windows[0].startMs)
        assertEquals(20_000L, windows[0].endMs)
        assertEquals(15_000L, windows[1].startMs)
        assertEquals(35_000L, windows[1].endMs)
    }

    @Test
    fun `MediaFrameScheduler schedules timestamps sensibly across short, medium, and long durations`() {
        assertEquals(listOf(0L), MediaFrameScheduler.computeSampleTimestampsMs(0L))

        val short10s = MediaFrameScheduler.computeSampleTimestampsMs(10_000L)
        assertTrue(short10s.size in 4..6)
        assertTrue(short10s.all { it in 0L..10_000L })

        val long600s = MediaFrameScheduler.computeSampleTimestampsMs(600_000L)
        assertEquals(30, long600s.size)
        assertTrue(long600s.first() >= 500L)
        assertTrue(long600s.last() <= 599_500L)
    }

    @Test
    fun `deduplicateAndRankMediaMatches groups video hits by file and keeps primary and distinct extra moments`() {
        val raw = listOf(
            MediaSearchResultItem("/Movies/trip.mp4", "", "VIDEO_FRAME", "trip.mp4", 45_000L, 50_000L, 120_000L, 0.91f),
            MediaSearchResultItem("/Movies/trip.mp4", "", "VIDEO_AUDIO", "trip.mp4", 46_000L, 66_000L, 120_000L, 0.85f), // < 4s apart from 45s -> skipped as extra
            MediaSearchResultItem("/Movies/trip.mp4", "", "VIDEO_FRAME", "trip.mp4", 90_000L, 95_000L, 120_000L, 0.82f), // >= 4s apart -> included
            MediaSearchResultItem("/DCIM/dog.jpg", "", "IMAGE", "dog.jpg", 0L, 0L, 0L, 0.88f)
        )

        val deduped = deduplicateAndRankMediaMatches(raw, topK = 5)
        assertEquals(2, deduped.size)
        assertEquals("/Movies/trip.mp4", deduped[0].mediaPath)
        assertEquals(45_000L, deduped[0].startMs)
        assertEquals(listOf(90_000L), deduped[0].extraMomentsMs)
        assertEquals("/DCIM/dog.jpg", deduped[1].mediaPath)
    }

    @Test
    fun `MediaIntentParser parses Burmese and English media search intents without hijacking contacts or SMS`() {
        val burmesePhoto = MediaIntentParser.parseNaturalMediaSearchIntent("ဖုန်းထဲက ပင်လယ်ကမ်းခြေ ပုံတွေ ရှာပေးပါ")
        assertNotNull(burmesePhoto)
        assertEquals(MediaModalityFilter.IMAGE, burmesePhoto!!.filter)
        assertTrue(burmesePhoto.query.contains("ပင်လယ်ကမ်းခြေ"))

        val burmeseVideo = MediaIntentParser.parseNaturalMediaSearchIntent("ခွေးပြေးနေတဲ့ video ရှာပေး")
        assertNotNull(burmeseVideo)
        assertEquals(MediaModalityFilter.VIDEO, burmeseVideo!!.filter)
        assertTrue(burmeseVideo.query.contains("ခွေးပြေးနေတဲ့"))

        val engAudio = MediaIntentParser.parseNaturalMediaSearchIntent("find voice memo about quarterly budget")
        assertNotNull(engAudio)
        assertEquals(MediaModalityFilter.AUDIO, engAudio!!.filter)
        assertEquals("quarterly budget", engAudio.query)

        // Negative cases: must return null
        assertNull(MediaIntentParser.parseNaturalMediaSearchIntent("ကိုကျော်ဇင် 09799123456 ကို contact ထည့်ပေးပါ"))
        assertNull(MediaIntentParser.parseNaturalMediaSearchIntent("send sms to U Ba"))
        assertNull(MediaIntentParser.parseNaturalMediaSearchIntent("check structural vibration"))
        assertNull(MediaIntentParser.parseNaturalMediaSearchIntent("tune my guitar"))
        assertNull(MediaIntentParser.parseNaturalMediaSearchIntent("ocr Screenshot.jpg"))
        assertNull(MediaIntentParser.parseNaturalMediaSearchIntent("/mediasearch sunset"))
    }

    @Test
    fun `selectVisionTokensPerImage clamps and aligns token budgets to model signatures`() {
        assertNull(InferenceService.selectVisionTokensPerImage(0, intArrayOf(70, 140, 280), 280))
        assertEquals(280, InferenceService.selectVisionTokensPerImage(280, intArrayOf(70, 140, 280, 560), 280))
        assertEquals(140, InferenceService.selectVisionTokensPerImage(280, intArrayOf(70, 140), 560))
        assertEquals(70, InferenceService.selectVisionTokensPerImage(100, intArrayOf(70, 140, 280), 280))
        assertEquals(140, InferenceService.selectVisionTokensPerImage(50, intArrayOf(140, 280), 280))
        assertEquals(140, InferenceService.selectVisionTokensPerImage(280, intArrayOf(70, 140, 280), 140))
        assertEquals(256, InferenceService.selectVisionTokensPerImage(280, IntArray(0), 256))
        assertNull(InferenceService.selectVisionTokensPerImage(280, IntArray(0), 0))
    }

    @Test
    fun `isEmbeddingModelFileName distinguishes EmbeddingGemma and Gecko files from LLMs`() {
        assertTrue(ModelDownloadManager.isEmbeddingModelFileName("embeddinggemma-2-740m.litertlm"))
        assertTrue(ModelDownloadManager.isEmbeddingModelFileName("embeddinggemma-2-text-270m.litertlm"))
        assertTrue(ModelDownloadManager.isEmbeddingModelFileName("Gecko-110m-en.litertlm"))
        assertFalse(ModelDownloadManager.isEmbeddingModelFileName("gemma-4-E2B-it.litertlm"))
        assertFalse(ModelDownloadManager.isEmbeddingModelFileName("Qwen3.5-0.8B_int8.litertlm"))
    }

    @Test
    fun `MediaModalityFilter VISUAL maps cleanly to native VISUAL SQL filter`() {
        assertEquals("VISUAL", MediaModalityFilter.VISUAL.sqlFilter)
        assertEquals("IMAGE", MediaModalityFilter.IMAGE.sqlFilter)
        assertEquals("VIDEO", MediaModalityFilter.VIDEO.sqlFilter)
        assertEquals("AUDIO", MediaModalityFilter.AUDIO.sqlFilter)
        assertEquals("ALL", MediaModalityFilter.ALL.sqlFilter)
    }

    @Test
    fun `isScreenshotAsset and isIgnoredMediaPath classify system and album paths accurately`() {
        assertTrue(isScreenshotAsset("/storage/emulated/0/DCIM/Screenshots/Screenshot_2026-11-22.png", "Screenshots", "Screenshot_2026-11-22.png"))
        assertTrue(isScreenshotAsset("/storage/emulated/0/Pictures/photo.jpg", "My Screenshots", "photo.jpg"))
        assertTrue(isScreenshotAsset("/storage/emulated/0/Download/screen_shot_1.png", "Download", "screen_shot_1.png"))
        assertFalse(isScreenshotAsset("/storage/emulated/0/DCIM/Camera/IMG_20261122.jpg", "Camera", "IMG_20261122.jpg"))

        assertTrue(isIgnoredMediaPath("/storage/emulated/0/Android/data/com.foo/cache/img.jpg"))
        assertTrue(isIgnoredMediaPath("/storage/emulated/0/DCIM/.thumbnails/1234.jpg"))
        assertTrue(isIgnoredMediaPath("/storage/emulated/0/WhatsApp/Media/WhatsApp Stickers/stk.webp"))
        assertFalse(isIgnoredMediaPath("/storage/emulated/0/DCIM/Camera/bougainvillea.jpg"))
    }

    @Test
    fun `prioritizeMediaAssets puts Camera before other albums and Screenshots last or excluded`() {
        val camOlder = DeviceMediaAsset("/DCIM/Camera/a.jpg", "", "IMAGE", "a.jpg", "Camera", 100L, 50000L, 0L)
        val camNewer = DeviceMediaAsset("/DCIM/Camera/b.jpg", "", "IMAGE", "b.jpg", "Camera", 300L, 50000L, 0L)
        val dl = DeviceMediaAsset("/Download/c.jpg", "", "IMAGE", "c.jpg", "Download", 500L, 50000L, 0L)
        val ss = DeviceMediaAsset("/DCIM/Screenshots/Screenshot_1.png", "", "IMAGE", "Screenshot_1.png", "Screenshots", 900L, 50000L, 0L)
        val aud = DeviceMediaAsset("/Recordings/note.m4a", "", "AUDIO", "note.m4a", "Recordings", 400L, 10000L, 5000L)

        val orderedAll = prioritizeMediaAssets(listOf(ss, dl, camOlder, aud, camNewer), excludeScreenshots = false)
        assertEquals(listOf(camNewer, camOlder, dl, aud, ss), orderedAll)

        val orderedNoSs = prioritizeMediaAssets(listOf(ss, dl, camOlder, aud, camNewer), excludeScreenshots = true)
        assertEquals(listOf(camNewer, camOlder, dl, aud), orderedNoSs)
        assertFalse(orderedNoSs.any { it.mediaPath == ss.mediaPath })
    }

    @Test
    fun `filterRelevantMediaMatches uses pure cosine ranking with relative top window and supports small corpora`() {
        // 1. Natural cross-modal matches (0.58..0.42) pass without destructive 0.692/0.71 cutoffs
        val flowerGallery = listOf(
            MediaSearchResultItem("/DCIM/Camera/flower1.jpg", "", "image", "flower1.jpg", 0L, 0L, 0L, 0.58f, cosineScore = 0.58f, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/flower2.jpg", "", "image", "flower2.jpg", 0L, 0L, 0L, 0.49f, cosineScore = 0.49f, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/flower3.jpg", "", "image", "flower3.jpg", 0L, 0L, 0L, 0.41f, cosineScore = 0.41f, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/unrelated.jpg", "", "image", "unrelated.jpg", 0L, 0L, 0L, 0.35f, cosineScore = 0.35f, assetType = "image")
        )
        // topCosine = 0.58 -> threshold = max(0.40, 0.58 - 0.20) = 0.40 -> top 3 kept, 0.35 dropped
        val filtered = filterRelevantMediaMatches(flowerGallery, MediaSearchQueryMode.TEXT_TO_MEDIA)
        assertEquals(
            listOf("/DCIM/Camera/flower1.jpg", "/DCIM/Camera/flower2.jpg", "/DCIM/Camera/flower3.jpg"),
            filtered.map { it.mediaPath }
        )

        // 2. Strong top result narrows relative window: top = 0.72 -> threshold = max(0.40, 0.52) = 0.52
        val tightWindow = listOf(
            MediaSearchResultItem("/DCIM/Camera/sim.jpg", "", "image", "sim.jpg", 0L, 0L, 0L, 0.72f, cosineScore = 0.72f, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/close.jpg", "", "image", "close.jpg", 0L, 0L, 0L, 0.54f, cosineScore = 0.54f, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/far.jpg", "", "image", "far.jpg", 0L, 0L, 0L, 0.48f, cosineScore = 0.48f, assetType = "image")
        )
        assertEquals(
            listOf("/DCIM/Camera/sim.jpg", "/DCIM/Camera/close.jpg"),
            filterRelevantMediaMatches(tightWindow, MediaSearchQueryMode.IMAGE_TO_VISUAL).map { it.mediaPath }
        )

        // 3. Small homogeneous manually-indexed set (N = 3) preserves all relevant matches even when Z-score <= 0
        val smallManualSet = listOf(
            MediaSearchResultItem("/DCIM/Camera/b1.jpg", "", "image", "b1.jpg", 0L, 0L, 0L, 0.56f, cosineScore = 0.56f, zScore = 0.6f, corpusMean = 0.54f, corpusStd = 0.02f, corpusSize = 3, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/b2.jpg", "", "image", "b2.jpg", 0L, 0L, 0L, 0.53f, cosineScore = 0.53f, zScore = -0.5f, corpusMean = 0.54f, corpusStd = 0.02f, corpusSize = 3, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/b3.jpg", "", "image", "b3.jpg", 0L, 0L, 0L, 0.51f, cosineScore = 0.51f, zScore = -1.5f, corpusMean = 0.54f, corpusStd = 0.02f, corpusSize = 3, assetType = "image")
        )
        assertEquals(3, filterRelevantMediaMatches(smallManualSet, MediaSearchQueryMode.TEXT_TO_MEDIA).size)

        // 4. Filename lexicalHits cannot bypass the semantic window
        val lexicalTrap = listOf(
            MediaSearchResultItem("/DCIM/Camera/true_match.jpg", "", "image", "IMG_1001.jpg", 0L, 0L, 0L, 0.65f, cosineScore = 0.65f, lexicalHits = 0, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/bougainvillea_fake.jpg", "", "image", "bougainvillea_fake.jpg", 0L, 0L, 0L, 0.35f, cosineScore = 0.35f, lexicalHits = 2, assetType = "image")
        )
        assertEquals(
            listOf("/DCIM/Camera/true_match.jpg"),
            filterRelevantMediaMatches(lexicalTrap, MediaSearchQueryMode.TEXT_TO_MEDIA).map { it.mediaPath }
        )

        // 5. Explicit lexical-only fallback (when query vector unavailable: cosineScore == 0f) still works
        val lexicalFallbackOnly = listOf(
            MediaSearchResultItem("/DCIM/Camera/bougainvillea_2026.jpg", "", "image", "bougainvillea_2026.jpg", 0L, 0L, 0L, 0.25f, cosineScore = 0f, lexicalHits = 1, assetType = "image")
        )
        assertEquals(1, filterRelevantMediaMatches(lexicalFallbackOnly, MediaSearchQueryMode.TEXT_TO_MEDIA).size)

        // 6. Deduplication & final ranking order strictly follows cosineScore descending
        val distortedScores = listOf(
            MediaSearchResultItem("/DCIM/Camera/low_cos_boosted.jpg", "", "image", "low.jpg", 0L, 0L, 0L, 0.99f, cosineScore = 0.46f, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/high_cos.jpg", "", "image", "high.jpg", 0L, 0L, 0L, 0.64f, cosineScore = 0.64f, assetType = "image")
        )
        assertEquals(
            listOf("/DCIM/Camera/high_cos.jpg", "/DCIM/Camera/low_cos_boosted.jpg"),
            deduplicateAndRankMediaMatches(distortedScores, topK = 5).map { it.mediaPath }
        )
    }

    @Test
    fun `TgaImageCodec writes uncompressed 32-bit BGRA Truevision TGA with top-left origin`() {
        val pixels = intArrayOf(
            0xFF112233.toInt(), // A=FF, R=11, G=22, B=33 -> BGRA bytes [33, 22, 11, FF]
            0x80AABBCC.toInt()  // A=80, R=AA, G=BB, B=CC -> BGRA bytes [CC, BB, AA, 80]
        )
        val tga = TgaImageCodec.encodeArgbPixelsToBgraTga(pixels, width = 2, height = 1)
        assertEquals(18 + 2 * 4, tga.size)
        assertEquals(2.toByte(), tga[2]) // uncompressed true-color
        assertEquals(2.toByte(), tga[12])
        assertEquals(0.toByte(), tga[13])
        assertEquals(1.toByte(), tga[14])
        assertEquals(0.toByte(), tga[15])
        assertEquals(32.toByte(), tga[16])
        assertEquals(0x28.toByte(), tga[17])

        // Pixel 0: B=0x33, G=0x22, R=0x11, A=0xFF
        assertEquals(0x33.toByte(), tga[18])
        assertEquals(0x22.toByte(), tga[19])
        assertEquals(0x11.toByte(), tga[20])
        assertEquals(0xFF.toByte(), tga[21])
        // Pixel 1: B=0xCC, G=0xBB, R=0xAA, A=0x80
        assertEquals(0xCC.toByte(), tga[22])
        assertEquals(0xBB.toByte(), tga[23])
        assertEquals(0xAA.toByte(), tga[24])
        assertEquals(0x80.toByte(), tga[25])
    }

    @Test
    fun `ExifOrientationHelper handles all 8 EXIF orientations and preserves aspect ratio up to 1024px`() {
        assertEquals(ExifTransformSpec(0f, false), ExifOrientationHelper.computeTransformSpec(ExifInterface.ORIENTATION_NORMAL))
        assertEquals(ExifTransformSpec(0f, true), ExifOrientationHelper.computeTransformSpec(ExifInterface.ORIENTATION_FLIP_HORIZONTAL))
        assertEquals(ExifTransformSpec(180f, false), ExifOrientationHelper.computeTransformSpec(ExifInterface.ORIENTATION_ROTATE_180))
        assertEquals(ExifTransformSpec(180f, true), ExifOrientationHelper.computeTransformSpec(ExifInterface.ORIENTATION_FLIP_VERTICAL))
        assertEquals(ExifTransformSpec(90f, true), ExifOrientationHelper.computeTransformSpec(ExifInterface.ORIENTATION_TRANSPOSE))
        assertEquals(ExifTransformSpec(90f, false), ExifOrientationHelper.computeTransformSpec(ExifInterface.ORIENTATION_ROTATE_90))
        assertEquals(ExifTransformSpec(270f, true), ExifOrientationHelper.computeTransformSpec(ExifInterface.ORIENTATION_TRANSVERSE))
        assertEquals(ExifTransformSpec(270f, false), ExifOrientationHelper.computeTransformSpec(ExifInterface.ORIENTATION_ROTATE_270))

        assertEquals(1024 to 768, ExifOrientationHelper.computeAspectPreservingSize(4000, 3000, MediaSearchEngine.PHOTO_MAX_SIDE_PX))
        assertEquals(768 to 1024, ExifOrientationHelper.computeAspectPreservingSize(3000, 4000, MediaSearchEngine.PHOTO_MAX_SIDE_PX))
        assertEquals(1024 to 1024, ExifOrientationHelper.computeAspectPreservingSize(2048, 2048, MediaSearchEngine.PHOTO_MAX_SIDE_PX))
        assertEquals(800 to 600, ExifOrientationHelper.computeAspectPreservingSize(800, 600, MediaSearchEngine.PHOTO_MAX_SIDE_PX))
    }

    @Test
    fun `excludeSeedMediaMatches removes query seed before ranking window so similar matches survive`() {
        val candidates = listOf(
            MediaSearchResultItem("/DCIM/Camera/seed.jpg", "content://media/external/images/media/42", "image", "seed.jpg", 0L, 0L, 0L, 1.0f, cosineScore = 1.0f, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/match_a.jpg", "content://media/external/images/media/43", "image", "match_a.jpg", 0L, 0L, 0L, 0.68f, cosineScore = 0.68f, assetType = "image"),
            MediaSearchResultItem("/DCIM/Camera/match_b.jpg", "content://media/external/images/media/44", "image", "match_b.jpg", 0L, 0L, 0L, 0.55f, cosineScore = 0.55f, assetType = "image")
        )

        val withoutSeedByPath = excludeSeedMediaMatches(candidates, seedPath = "/DCIM/Camera/seed.jpg", seedUri = "")
        assertEquals(listOf("/DCIM/Camera/match_a.jpg", "/DCIM/Camera/match_b.jpg"), withoutSeedByPath.map { it.mediaPath })

        val withoutSeedByUri = excludeSeedMediaMatches(
            candidates,
            seedPath = "",
            seedUri = "content://media/external/images/media/42"
        )
        assertEquals(listOf("/DCIM/Camera/match_a.jpg", "/DCIM/Camera/match_b.jpg"), withoutSeedByUri.map { it.mediaPath })

        // Because seed (1.0) was stripped first, topCosine = 0.68 -> threshold = 0.48 -> both similar matches pass
        val filtered = filterRelevantMediaMatches(withoutSeedByUri, MediaSearchQueryMode.IMAGE_TO_VISUAL)
        assertEquals(listOf("/DCIM/Camera/match_a.jpg", "/DCIM/Camera/match_b.jpg"), filtered.map { it.mediaPath })
    }

    @Test
    fun `buildEmbeddingCacheSubdirName isolates caches per backend tokens and maxInputLength`() {
        val mediaGpu = InferenceService.buildEmbeddingCacheSubdirName(
            "embeddinggemma-2-740m.litertlm",
            hasVision = true,
            visionIsGpu = true,
            hasAudio = true,
            visionTokensPerImage = InferenceService.MEDIA_VISION_TOKENS_PER_IMAGE,
            maxInputLength = InferenceService.MEDIA_MAX_INPUT_LENGTH
        )
        val mediaTextOnly = InferenceService.buildEmbeddingCacheSubdirName(
            "embeddinggemma-2-740m.litertlm",
            hasVision = false,
            visionIsGpu = false,
            hasAudio = false,
            visionTokensPerImage = null,
            maxInputLength = InferenceService.MEDIA_MAX_INPUT_LENGTH
        )
        val docDefault = InferenceService.buildEmbeddingCacheSubdirName(
            "embeddinggemma-2-740m.litertlm",
            hasVision = false,
            visionIsGpu = false,
            hasAudio = false,
            visionTokensPerImage = null,
            maxInputLength = null
        )
        assertNotEquals(mediaGpu, mediaTextOnly)
        assertNotEquals(mediaTextOnly, docDefault)
        assertTrue(mediaGpu.contains("vt70"))
        assertTrue(mediaGpu.contains("len256"))
        assertTrue(docDefault.contains("lendef"))
    }

    @Test
    fun `isRuntimeVersionCompatible safely checks semver requirements`() {
        assertTrue(InferenceService.isRuntimeVersionCompatible("0.19.1", ""))
        assertTrue(InferenceService.isRuntimeVersionCompatible("0.19.1", "0.19.0"))
        assertTrue(InferenceService.isRuntimeVersionCompatible("0.19.1", "v0.19.1"))
        assertTrue(InferenceService.isRuntimeVersionCompatible("0.19.1", "0.19.1-rc1"))
        assertTrue(InferenceService.isRuntimeVersionCompatible("0.19.1", "0.18.9+build5"))

        assertFalse(InferenceService.isRuntimeVersionCompatible("0.19.1", "0.19.2"))
        assertFalse(InferenceService.isRuntimeVersionCompatible("0.19.1", "0.20.0"))
        assertFalse(InferenceService.isRuntimeVersionCompatible("0.19.1-alpha", "0.19.1"))
        assertFalse(InferenceService.isRuntimeVersionCompatible("", "0.19.0"))
    }

    @Test
    fun `enforceStrictModalityIsolation blocks video frames audio and text from image results`() {
        val mixed = listOf(
            MediaSearchResultItem("/DCIM/Camera/photo.jpg", "", "image", "photo.jpg", 0L, 0L, 0L, 0.85f, assetType = "image"),
            MediaSearchResultItem("/Movies/clip.mp4", "", "image", "clip.mp4", 1000L, 5000L, 10000L, 0.99f, assetType = "video"),
            MediaSearchResultItem("/Movies/clip.mp4", "", "audio", "clip.mp4", 0L, 10000L, 10000L, 0.92f, assetType = "video"),
            MediaSearchResultItem("/Movies/legacy_clip.mp4", "", "VIDEO_FRAME", "legacy_clip.mp4", 0L, 5000L, 5000L, 0.98f),
            MediaSearchResultItem("/Recordings/note.m4a", "", "audio", "note.m4a", 0L, 8000L, 8000L, 0.88f, assetType = "audio"),
            MediaSearchResultItem("/Docs/spec.pdf", "", "text", "spec.pdf", 0L, 0L, 0L, 0.90f, assetType = "text")
        )

        val imagesOnly = enforceStrictModalityIsolation(mixed, allowedModality = "image", allowedAssetType = "image")
        assertEquals(listOf("/DCIM/Camera/photo.jpg"), imagesOnly.map { it.mediaPath })

        val audioOnly = enforceStrictModalityIsolation(mixed, allowedModality = "audio", allowedAssetType = "audio")
        assertEquals(listOf("/Recordings/note.m4a"), audioOnly.map { it.mediaPath })

        val videoFramesOnly = enforceStrictModalityIsolation(mixed, allowedModality = "image", allowedAssetType = "video")
        assertEquals(listOf("/Movies/clip.mp4", "/Movies/legacy_clip.mp4"), videoFramesOnly.map { it.mediaPath })

        val docsOnly = enforceStrictModalityIsolation(mixed, allowedModality = "text", allowedAssetType = "text")
        assertEquals(listOf("/Docs/spec.pdf"), docsOnly.map { it.mediaPath })
    }

    @Test
    fun `shouldReuseCachedEmbeddingEngine reuses matching media and doc profiles while isolating mismatched budgets`() {
        // 1. Consecutive cold TASK_NONE text queries reuse text-only engine with maxInputLength=256
        assertTrue(
            InferenceService.shouldReuseCachedEmbeddingEngine(
                sameModelPath = true,
                requireVision = false,
                requireAudio = false,
                forceVisionCpu = false,
                cachedVisionEnabled = false,
                cachedAudioEnabled = false,
                cachedVisionIsGpu = false,
                cachedConfigVisionTokens = null,
                cachedMaxInputLength = 256,
                requestedVisionTokens = 70,
                requestedMaxInputLength = 256
            )
        )

        // 2. Warm multimodal engine (vision=70, len=256) serves subsequent TASK_NONE text & image queries
        assertTrue(
            InferenceService.shouldReuseCachedEmbeddingEngine(
                sameModelPath = true,
                requireVision = false,
                requireAudio = false,
                forceVisionCpu = false,
                cachedVisionEnabled = true,
                cachedAudioEnabled = true,
                cachedVisionIsGpu = true,
                cachedConfigVisionTokens = 70,
                cachedMaxInputLength = 256,
                requestedVisionTokens = 70,
                requestedMaxInputLength = 256
            )
        )

        // 3. GPU -> CPU vision fallback invalidates cached GPU vision engine
        assertFalse(
            InferenceService.shouldReuseCachedEmbeddingEngine(
                sameModelPath = true,
                requireVision = true,
                requireAudio = false,
                forceVisionCpu = true,
                cachedVisionEnabled = true,
                cachedAudioEnabled = false,
                cachedVisionIsGpu = true,
                cachedConfigVisionTokens = 70,
                cachedMaxInputLength = 256,
                requestedVisionTokens = 70,
                requestedMaxInputLength = 256
            )
        )

        // 4. Document RAG (maxInputLength=null) never reuses truncated media engine (maxInputLength=256)
        assertFalse(
            InferenceService.shouldReuseCachedEmbeddingEngine(
                sameModelPath = true,
                requireVision = false,
                requireAudio = false,
                forceVisionCpu = false,
                cachedVisionEnabled = true,
                cachedAudioEnabled = false,
                cachedVisionIsGpu = false,
                cachedConfigVisionTokens = 70,
                cachedMaxInputLength = 256,
                requestedVisionTokens = null,
                requestedMaxInputLength = null
            )
        )
    }
}
