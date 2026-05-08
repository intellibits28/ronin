package com.ronin.kernel

/**
 * Data class mapped from Native C++ InferencePacket (Phase 1).
 * Used for streaming UTF-8 fragments (Burmese support).
 */
data class InferencePacket(
    val tokenId: Int,
    val fragment: String,
    val isFinal: Boolean
)
