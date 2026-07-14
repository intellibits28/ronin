package com.ronin.kernel.shm

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/**
 * Requirement 4: Unit Testing Implementation - ShmAiReviewPipeline & Payload Truncation
 * Pure Kotlin JUnit test verifying AI payload optimization, peak extraction, and downsampling.
 */
class ShmAiReviewPipelineTest {

    @Test
    fun testOptimizedAiPayloadTruncatesRawArraysAndExtractsTopPeaks() {
        // Create 1024-point dummy PSD spectrum with distinct peaks
        val dummyPsd = MutableList(1024) { 0.001f }
        dummyPsd[50] = 0.85f   // Peak 1 (~2.44 Hz)
        dummyPsd[120] = 0.42f  // Peak 2 (~5.86 Hz)
        dummyPsd[200] = 0.31f  // Peak 3 (~9.76 Hz)

        val session = ShmSession(
            deviceProfile = DeviceProfile(samplingRate = 100.0f),
            sensorMetadata = SensorMetadata(duration = 10.24f, sampleCount = 1024),
            dspResult = DspResult(
                fftResult = MutableList(1024) { 0.1f },
                psdResult = dummyPsd
            )
        )

        val payloadStr = session.toOptimizedAiPayload(ExportOptions(includeRawSensorData = false))
        val json = JSONObject(payloadStr)
        val dsp = json.getJSONObject("dspResult")

        // Verify massive raw arrays are stripped
        assertFalse("Massive fftResult should not be sent to AI", dsp.has("fftResult"))
        assertFalse("Massive psdResult should not be sent to AI", dsp.has("psdResult"))

        // Verify top spectral peaks are extracted
        assertTrue("extractedTopSpectralPeaks should be generated", dsp.has("extractedTopSpectralPeaks"))
        val peaks = dsp.getJSONArray("extractedTopSpectralPeaks")
        assertTrue("Must extract top peaks", peaks.length() > 0 && peaks.length() <= 5)

        // Verify highest peak value matches dummyPsd[50] = 0.85
        val topPeak = peaks.getJSONObject(0)
        assertEquals(0.85, topPeak.getDouble("psd_value"), 0.001)
    }

    @Test
    fun testOptimizedAiPayloadDownsamplesToAtMost16BinsWhenRawEnabled() {
        val largePsd = MutableList(2048) { it * 0.001f }
        val session = ShmSession(
            dspResult = DspResult(psdResult = largePsd)
        )

        val payloadStr = session.toOptimizedAiPayload(ExportOptions(includeRawSensorData = true))
        val json = JSONObject(payloadStr)
        val dsp = json.getJSONObject("dspResult")

        assertTrue("Downsampled vector should be included when raw enabled", dsp.has("downsampledPsdVector_16bins"))
        val downsampled = dsp.getJSONArray("downsampledPsdVector_16bins")
        assertTrue("Downsampled vector must not exceed 16 bins", downsampled.length() <= 16)
    }
}
