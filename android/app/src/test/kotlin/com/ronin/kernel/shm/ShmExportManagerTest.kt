package com.ronin.kernel.shm

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/**
 * Requirement 4: Unit Testing Implementation - ShmExportManager & Privacy logic
 * Pure Kotlin JUnit test class verifying JSON schema versioning and privacy exclusions.
 */
class ShmExportManagerTest {

    @Test
    fun testSchemaVersionIsSerializedInEngineeringJson() {
        val session = ShmSession(schemaVersion = "1.0")
        val jsonStr = session.toEngineeringJson(ExportOptions())
        val json = JSONObject(jsonStr)

        assertTrue("Engineering JSON should contain schemaVersion", json.has("schemaVersion"))
        assertEquals("Schema version must match 1.0", "1.0", json.getString("schemaVersion"))
    }

    @Test
    fun testPrivacyTogglesExcludeSensitiveData() {
        val session = ShmSession(
            deviceProfile = DeviceProfile(model = "Pixel 8 Pro", sensorType = "Bosch BMI260"),
            locationProfile = LocationProfile(buildingId = "BLDG-SECRET-99", registeredLocation = "Lab 3")
        )

        // Exclude GPS/Location & Device Identifier
        val options = ExportOptions(
            includeGpsLocation = false,
            includeDeviceIdentifier = false,
            includeRawSensorData = false
        )

        val jsonStr = session.toEngineeringJson(options)
        val json = JSONObject(jsonStr)

        // Verify locationProfile is stripped completely
        assertFalse("Location profile should be removed when includeGpsLocation is false", json.has("locationProfile"))

        // Verify deviceProfile model is anonymized
        assertTrue("Device profile should exist", json.has("deviceProfile"))
        val dev = json.getJSONObject("deviceProfile")
        assertEquals("Device model should be anonymized", "ANONYMIZED_DEVICE", dev.getString("model"))

        // Verify raw sensor arrays are excluded
        val dsp = json.optJSONObject("dspResult")
        if (dsp != null) {
            assertFalse("Raw fftResult should be removed when includeRawSensorData is false", dsp.has("fftResult"))
            assertFalse("Raw psdResult should be removed when includeRawSensorData is false", dsp.has("psdResult"))
        }
    }

    @Test
    fun testFullEngineeringJsonIncludesAllSectionsWhenPrivacyAllows() {
        val session = ShmSession()
        val options = ExportOptions(
            includeGpsLocation = true,
            includeDeviceIdentifier = true,
            includeRawSensorData = true,
            includeAnalysisMetrics = true,
            includeDspResults = true,
            includeDecisionResult = true
        )

        val jsonStr = session.toEngineeringJson(options)
        val json = JSONObject(jsonStr)

        assertTrue(json.has("locationProfile"))
        assertTrue(json.has("deviceProfile"))
        assertTrue(json.has("features"))
        assertTrue(json.has("dspResult"))
        assertTrue(json.has("decision"))
        assertEquals("1.0", json.getString("schemaVersion"))
    }
}
