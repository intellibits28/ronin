package com.ronin.kernel

import android.content.Context
import android.telephony.SmsManager
import android.os.Build
import org.json.JSONObject

/**
 * v7.0 Driver: Handles SMS composition and sending.
 */
class SmsDriver(private val context: Context) : ICapabilityDriver {
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
            val smsManager = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                context.getSystemService(SmsManager::class.java)
            } else {
                @Suppress("DEPRECATION")
                SmsManager.getDefault()
            }
            
            smsManager?.sendTextMessage(recipient, null, message, null, null)
            
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
