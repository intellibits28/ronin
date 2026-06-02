package com.ronin.kernel

import android.telephony.SmsManager
import org.json.JSONObject

/**
 * v7.0 Driver: Handles SMS composition and sending.
 */
class SmsDriver : ICapabilityDriver {
    override fun execute(request: JSONObject): JSONObject {
        val response = JSONObject()
        return try {
            val recipient = request.optString("recipient", "")
            val message = request.optString("message", "")
            
            if (recipient.isEmpty() || message.isEmpty()) {
                response.put("success", false)
                response.put("error", "Missing recipient or message")
                return response
            }

            // Using modern SmsManager (requires SEND_SMS permission)
            val smsManager = SmsManager.getDefault()
            smsManager.sendTextMessage(recipient, null, message, null, null)
            
            response.put("success", true)
            response.put("status", "SMS sent successfully")
            response
        } catch (e: Exception) {
            response.put("success", false)
            response.put("error", e.message ?: "SMS sending failed")
            response
        }
    }
}
