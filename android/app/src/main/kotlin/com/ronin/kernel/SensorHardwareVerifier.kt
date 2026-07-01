package com.ronin.kernel

import android.hardware.Sensor
import android.util.Log
import org.json.JSONObject
import kotlin.math.abs
import kotlin.math.sqrt

/**
 * Utility class to verify Android hardware sensor capabilities, stability, and uniformity.
 */
object SensorHardwareVerifier {
    private const val TAG = "RoninSensorVerifier"

    /**
     * 1. Calculate the theoretical maximum sampling rate (in Hz) based on sensor minDelay.
     * @param sensor The target hardware sensor (e.g., Accelerometer).
     * @return Max frequency in Hz, or 0.0f if continuous/unspecified.
     */
    fun getMaxSamplingRateHz(sensor: Sensor?): Float {
        if (sensor == null) return 0.0f
        val minDelayUs = sensor.minDelay
        return if (minDelayUs > 0) {
            1_000_000.0f / minDelayUs.toFloat()
        } else {
            // minDelay == 0 implies continuous event streaming at maximum hardware speed
            0.0f
        }
    }

    /**
     * 2. Check if timestamps provided by SensorEvent are consistent and free of excessive scheduler jitter.
     * @param timestampsNs Array of sequential event.timestamp values in nanoseconds.
     * @param targetHz Expected sampling frequency in Hz (default: 100Hz).
     * @param allowedJitterPct Maximum allowed standard deviation percentage (default: 15%).
     * @return true if hardware resolution and timing are stable within threshold.
     */
    fun isHardwareResolutionStable(timestampsNs: LongArray, targetHz: Float = 100.0f, allowedJitterPct: Float = 15.0f): Boolean {
        if (timestampsNs.size < 3) return false

        val intervalsMs = FloatArray(timestampsNs.size - 1)
        var sumMs = 0.0f
        for (i in 0 until timestampsNs.size - 1) {
            val dtNs = timestampsNs[i + 1] - timestampsNs[i]
            val dtMs = dtNs / 1_000_000.0f
            intervalsMs[i] = dtMs
            sumMs += dtMs
        }

        val meanIntervalMs = sumMs / intervalsMs.size
        var sqDiffSum = 0.0f
        for (dt in intervalsMs) {
            val diff = dt - meanIntervalMs
            sqDiffSum += diff * diff
        }

        val stdDevMs = sqrt(sqDiffSum / intervalsMs.size)
        val jitterPct = (stdDevMs / meanIntervalMs) * 100.0f

        val expectedIntervalMs = 1000.0f / targetHz
        val intervalDriftPct = abs(meanIntervalMs - expectedIntervalMs) / expectedIntervalMs * 100.0f

        Log.i(TAG, "Hardware Stability Check -> Mean Interval: %.2f ms (Expected: %.2f ms), StdDev Jitter: %.2f%% (Allowed: %.2f%%)".format(
            meanIntervalMs, expectedIntervalMs, jitterPct, allowedJitterPct
        ))

        return jitterPct <= allowedJitterPct && intervalDriftPct <= 25.0f
    }

    /**
     * 3. Log and dump all available hardware sensor properties to diagnose physical limits.
     */
    fun dumpSensorCapabilities(sensor: Sensor?): JSONObject {
        val json = JSONObject()
        if (sensor == null) {
            json.put("error", "SENSOR_UNAVAILABLE")
            return json
        }

        val maxHz = getMaxSamplingRateHz(sensor)
        json.put("name", sensor.name)
        json.put("vendor", sensor.vendor)
        json.put("version", sensor.version)
        json.put("type", sensor.type)
        json.put("maxRange_m_s2", sensor.maximumRange)
        json.put("resolution_m_s2", sensor.resolution)
        json.put("power_mA", sensor.power)
        json.put("minDelay_us", sensor.minDelay)
        json.put("maxDelay_us", sensor.maxDelay)
        json.put("fifoReservedEventCount", sensor.fifoReservedEventCount)
        json.put("fifoMaxEventCount", sensor.fifoMaxEventCount)
        json.put("calculatedMaxSamplingRate_Hz", maxHz)

        Log.i(TAG, "=== Hardware Sensor Dump ===")
        Log.i(TAG, json.toString(2))
        return json
    }

    /**
     * 4. Linear Interpolation (LERP) Resampler: Re-aligns uneven hardware interrupt timestamps 
     * into a uniform time grid (e.g., exactly 100Hz / 10ms intervals) for FFT accuracy in C++.
     */
    class UniformResampler(private val targetHz: Float = 100.0f) {
        private val targetIntervalNs: Long = (1_000_000_000.0 / targetHz).toLong()
        private var nextTargetTimeNs: Long = -1L

        private var lastTimeNs: Long = -1L
        private var lastX: Float = 0.0f
        private var lastY: Float = 0.0f
        private var lastZ: Float = 0.0f

        data class Sample3D(val x: Float, val y: Float, val z: Float)

        /**
         * Feed incoming raw SensorEvent timestamp and values.
         * Returns a list of uniformly interpolated samples ready for VibeMonitorEngine.
         */
        fun pushRawEvent(timestampNs: Long, x: Float, y: Float, z: Float): List<Sample3D> {
            val uniformSamples = mutableListOf<Sample3D>()

            if (lastTimeNs == -1L || nextTargetTimeNs == -1L) {
                lastTimeNs = timestampNs
                lastX = x; lastY = y; lastZ = z
                nextTargetTimeNs = timestampNs + targetIntervalNs
                return uniformSamples
            }

            // Interpolate all uniform grid ticks that fell between lastTimeNs and current timestampNs
            while (nextTargetTimeNs <= timestampNs) {
                val dtTotal = timestampNs - lastTimeNs
                val alpha = if (dtTotal > 0) {
                    ((nextTargetTimeNs - lastTimeNs).toDouble() / dtTotal.toDouble()).toFloat()
                } else 1.0f

                val interpX = lastX + alpha * (x - lastX)
                val interpY = lastY + alpha * (y - lastY)
                val interpZ = lastZ + alpha * (z - lastZ)

                uniformSamples.add(Sample3D(interpX, interpY, interpZ))
                nextTargetTimeNs += targetIntervalNs
            }

            lastTimeNs = timestampNs
            lastX = x; lastY = y; lastZ = z
            return uniformSamples
        }

        fun reset() {
            nextTargetTimeNs = -1L
            lastTimeNs = -1L
        }
    }
}
