package com.ronin.kernel

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
            InferenceService.formatForEmbeddingGemma2("raw passthrough", InferenceService.TASK_RAW)
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
    fun `filterRelevantMediaMatches rejects flat 0_68 background noise while keeping real matches`() {
        // 1. Flat 68% screenshot noise across 60 scanned photos -> all rejected
        val flatNoise = (1..8).map { idx ->
            MediaSearchResultItem(
                mediaPath = "/DCIM/Screenshots/s$idx.png",
                mediaUri = "",
                modality = "IMAGE",
                displayName = "s$idx.png",
                startMs = 0L,
                endMs = 0L,
                durationMs = 0L,
                score = 0.68f - idx * 0.002f,
                cosineScore = 0.68f - idx * 0.002f,
                zScore = 1.6f - idx * 0.2f,
                corpusMean = 0.655f,
                corpusStd = 0.015f,
                corpusSize = 60
            )
        }
        assertTrue(filterRelevantMediaMatches(flatNoise, MediaSearchQueryMode.TEXT_TO_MEDIA).isEmpty())

        // 2. Real gallery with 488 items: 0.75..0.70 bougainvillea hits pass while 0.665 background fails
        val galleryHits = listOf(
            MediaSearchResultItem("/DCIM/Camera/flower1.jpg", "", "IMAGE", "flower1.jpg", 0L, 0L, 0L, 0.75f, cosineScore = 0.75f, zScore = 2.54f, corpusMean = 0.656f, corpusStd = 0.037f, corpusSize = 488),
            MediaSearchResultItem("/DCIM/Camera/flower2.jpg", "", "IMAGE", "flower2.jpg", 0L, 0L, 0L, 0.70f, cosineScore = 0.70f, zScore = 1.19f, corpusMean = 0.656f, corpusStd = 0.037f, corpusSize = 488),
            MediaSearchResultItem("/DCIM/Screenshots/bg.png", "", "IMAGE", "bg.png", 0L, 0L, 0L, 0.665f, cosineScore = 0.665f, zScore = 0.24f, corpusMean = 0.656f, corpusStd = 0.037f, corpusSize = 488)
        )
        val filteredGallery = filterRelevantMediaMatches(galleryHits, MediaSearchQueryMode.TEXT_TO_MEDIA)
        assertEquals(2, filteredGallery.size)
        assertEquals(listOf("/DCIM/Camera/flower1.jpg", "/DCIM/Camera/flower2.jpg"), filteredGallery.map { it.mediaPath })

        // 3. Small homogeneous manually-picked set (6 photos all strong matches >= highFloor)
        val smallManualSet = listOf(
            MediaSearchResultItem("/DCIM/Camera/b1.jpg", "", "IMAGE", "b1.jpg", 0L, 0L, 0L, 0.74f, cosineScore = 0.74f, zScore = 0.6f, corpusMean = 0.73f, corpusStd = 0.01f, corpusSize = 6),
            MediaSearchResultItem("/DCIM/Camera/b2.jpg", "", "IMAGE", "b2.jpg", 0L, 0L, 0L, 0.72f, cosineScore = 0.72f, zScore = -0.9f, corpusMean = 0.73f, corpusStd = 0.01f, corpusSize = 6)
        )
        assertEquals(2, filterRelevantMediaMatches(smallManualSet, MediaSearchQueryMode.TEXT_TO_MEDIA).size)

        // 4. Visual similarity uses higher threshold, audio uses lower cross-modal threshold, lexical matches kept
        val visualMixed = listOf(
            MediaSearchResultItem("/DCIM/Camera/sim.jpg", "", "IMAGE", "sim.jpg", 0L, 0L, 0L, 0.84f, cosineScore = 0.84f, zScore = 2.0f, corpusMean = 0.68f, corpusStd = 0.08f, corpusSize = 40),
            MediaSearchResultItem("/DCIM/Camera/weak.jpg", "", "IMAGE", "weak.jpg", 0L, 0L, 0L, 0.70f, cosineScore = 0.70f, zScore = 0.25f, corpusMean = 0.68f, corpusStd = 0.08f, corpusSize = 40)
        )
        assertEquals(listOf("/DCIM/Camera/sim.jpg"), filterRelevantMediaMatches(visualMixed, MediaSearchQueryMode.IMAGE_TO_VISUAL).map { it.mediaPath })

        val audioMixed = listOf(
            MediaSearchResultItem("/Recordings/meeting.m4a", "", "AUDIO", "meeting.m4a", 0L, 20000L, 20000L, 0.67f, cosineScore = 0.67f, zScore = 1.8f, corpusMean = 0.52f, corpusStd = 0.05f, corpusSize = 15),
            MediaSearchResultItem("/Recordings/other.m4a", "", "AUDIO", "other.m4a", 0L, 20000L, 20000L, 0.54f, cosineScore = 0.54f, zScore = 0.4f, corpusMean = 0.52f, corpusStd = 0.05f, corpusSize = 15)
        )
        assertEquals(listOf("/Recordings/meeting.m4a"), filterRelevantMediaMatches(audioMixed, MediaSearchQueryMode.TEXT_TO_MEDIA).map { it.mediaPath })

        val lexicalMatch = listOf(
            MediaSearchResultItem("/DCIM/Camera/bougainvillea_2026.jpg", "", "image", "bougainvillea_2026.jpg", 0L, 0L, 0L, 0.71f, cosineScore = 0.63f, lexicalHits = 1, corpusSize = 30, assetType = "image")
        )
        assertEquals(1, filterRelevantMediaMatches(lexicalMatch, MediaSearchQueryMode.TEXT_TO_MEDIA).size)
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
}
