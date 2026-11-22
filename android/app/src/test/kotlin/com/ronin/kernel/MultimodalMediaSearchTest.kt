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
}
