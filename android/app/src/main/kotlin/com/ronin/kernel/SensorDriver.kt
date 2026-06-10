package com.ronin.kernel

import android.content.Context
import android.hardware.Sensor
import android.content.Context.SENSOR_SERVICE
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import org.json.JSONObject
import android.util.Log

/**
 * v1.0 Sensor DSP Driver: Collects and batches accelerometer data for native processing.
 */
class SensorDriver(private val context: Context, private val nativeEngine: NativeEngine) : ICapabilityDriver, SensorEventListener {
    private val TAG = "RoninSensorDriver"
    private var sensorManager: SensorManager = context.getSystemService(SENSOR_SERVICE) as SensorManager
    private var accelerometer: Sensor? = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
    
    private val BATCH_SIZE = 1024
    private val bufferX = FloatArray(BATCH_SIZE)
    private val bufferY = FloatArray(BATCH_SIZE)
    private val bufferZ = FloatArray(BATCH_SIZE)
    private var bufferIndex = 0

    init {
        startCollecting()
    }

    fun startCollecting() {
        accelerometer?.let {
            sensorManager.registerListener(this, it, SensorManager.SENSOR_DELAY_FASTEST)
            Log.i(TAG, "Sensor collection started.")
        } ?: Log.e(TAG, "Accelerometer not found!")
    }

    fun stopCollecting() {
        sensorManager.unregisterListener(this)
        Log.i(TAG, "Sensor collection stopped.")
    }

    override fun onSensorChanged(event: SensorEvent?) {
        if (event?.sensor?.type == Sensor.TYPE_ACCELEROMETER) {
            bufferX[bufferIndex] = event.values[0]
            bufferY[bufferIndex] = event.values[1]
            bufferZ[bufferIndex] = event.values[2]
            bufferIndex++

            if (bufferIndex >= BATCH_SIZE) {
                // Push batch to C++
                nativeEngine.pushSensorSamples(bufferX.clone(), bufferY.clone(), bufferZ.clone(), "accelerometer")
                bufferIndex = 0
            }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}

    override fun execute(request: JSONObject): JSONObject {
        val action = request.optString("action", "get_analysis")
        val sensorType = request.optString("sensor_type", "accelerometer")

        return when (action) {
            "get_analysis", "get_sensor_analysis" -> {
                val analysisJson = nativeEngine.getSensorAnalysis(sensorType)
                try {
                    JSONObject(analysisJson)
                } catch (e: Exception) {
                    JSONObject().put("error", "PARSE_ERROR").put("message", e.message)
                }
            }
            else -> JSONObject().put("error", "UNKNOWN_ACTION").put("message", "Action $action not supported")
        }
    }
}
