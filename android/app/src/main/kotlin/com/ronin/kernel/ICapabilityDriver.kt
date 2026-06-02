package com.ronin.kernel

import org.json.JSONObject

/**
 * v7.0 Layer 4: Base interface for all Android-specific capability drivers.
 */
interface ICapabilityDriver {
    /**
     * Executes the requested action using Android system APIs.
     * @param request The JSON payload containing parameters.
     * @return A JSON object containing the result or error.
     */
    fun execute(request: JSONObject): JSONObject
}
