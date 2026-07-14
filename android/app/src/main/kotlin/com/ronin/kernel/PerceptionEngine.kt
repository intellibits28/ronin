package com.ronin.kernel

import android.content.Context
import android.util.Log
import org.json.JSONObject
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import android.content.ContentValues

/**
 * Ronin Cognitive Sensor Runtime Blueprint v2.0
 * Perception Engine & Sensor Fusion: Classifies user activity and environmental status
 * at 10Hz and updates both BeliefState and SQLite.
 */
class PerceptionEngine(private val context: Context, private val nativeEngine: NativeEngine) {
    private val TAG = "RoninPerception"
    private val scheduler = Executors.newSingleThreadScheduledExecutor()
    private val dbHelper = DatabaseHelper(context)

    private var lastState = "UNKNOWN"

    init {
        // Run state machine at 10Hz (every 100ms)
        scheduler.scheduleAtFixedRate({
            runPerceptionLoop()
        }, 0, 100, TimeUnit.MILLISECONDS)
        Log.i(TAG, "Perception Engine initialized at 10Hz.")
    }

    private fun runPerceptionLoop() {
        try {
            // 1. Fetch sensor analysis from Native DSP
            val analysisJson = nativeEngine.getSensorAnalysis("accelerometer")
            (context as? MainActivity)?.runOnUiThread {
                try {
                    androidx.lifecycle.ViewModelProvider(context)[ChatViewModel::class.java].updateShmMetricsFromJson(analysisJson)
                } catch (_: Exception) {}
            }
            val json = JSONObject(analysisJson)
            
            // Extract features
            val resonanceFreq = json.optDouble("resonance_freq_hz", 0.0)
            val psdPeak = json.optDouble("psd_peak_db", -100.0)
            val anomalyDetected = json.optBoolean("anomaly_detected", false)

            // Dynamic heuristic-based context classification
            var currentState = "phone_on_table" // Default/Quiet baseline
            
            if (psdPeak > -40.0 && resonanceFreq > 0.5) {
                if (resonanceFreq in 1.5..3.0) {
                    currentState = "walking"
                } else if (resonanceFreq > 3.0) {
                    currentState = "running"
                } else if (anomalyDetected) {
                    currentState = "building_vibration"
                } else {
                    currentState = "active"
                }
            } else if (psdPeak > -60.0) {
                currentState = "phone_in_pocket"
            }

            // 2. If state changed, update belief state and SQLite
            if (currentState != lastState) {
                lastState = currentState
                Log.i(TAG, "Perception State Shifted -> $currentState")
                
                // Store in BeliefState via Native JNI
                nativeEngine.updateBelief("physical_activity", currentState, 1.0f)
                
                // Store in SQLite perception_history
                storePerceptionState(currentState)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error in Perception Loop: ${e.message}")
        }
    }

    private fun storePerceptionState(state: String) {
        try {
            val db = dbHelper.writableDatabase
            val values = ContentValues().apply {
                put("timestamp", System.currentTimeMillis() / 1000)
                put("state_type", "physical_activity")
                put("state_value", state)
            }
            db.insert("perception_history", null, values)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to write perception state to SQLite: ${e.message}")
        }
    }

    fun shutdown() {
        scheduler.shutdown()
        Log.i(TAG, "Perception Engine shutdown.")
    }
}
