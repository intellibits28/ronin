package com.ronin.kernel

import android.content.Context
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority
import com.google.android.gms.tasks.Tasks
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/**
 * v7.0 Driver: Handles GPS/Network location retrieval.
 */
class LocationDriver(private val context: Context) : ICapabilityDriver {
    override fun execute(request: JSONObject): JSONObject {
        val response = JSONObject()
        return try {
            val fusedLocationClient = LocationServices.getFusedLocationProviderClient(context)
            // Use Tasks.await for synchronous-like execution within the driver thread
            val location = Tasks.await(
                fusedLocationClient.getCurrentLocation(Priority.PRIORITY_HIGH_ACCURACY, null),
                30, TimeUnit.SECONDS
            )
            
            if (location != null) {
                response.put("success", true)
                response.put("lat", location.latitude)
                response.put("lon", location.longitude)
            } else {
                response.put("success", false)
                response.put("error", "Location unavailable")
            }
            response
        } catch (e: Exception) {
            response.put("success", false)
            response.put("error", e.message ?: "Unknown location error")
            response
        }
    }
}
