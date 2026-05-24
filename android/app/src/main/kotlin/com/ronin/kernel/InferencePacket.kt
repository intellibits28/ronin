package com.ronin.kernel

/**
 * Data class for streaming UTF-8 fragments (Burmese support).
 */
data class InferencePacket(
    val tokenId: Int,
    val fragment: String,
    val isFinal: Boolean
)
