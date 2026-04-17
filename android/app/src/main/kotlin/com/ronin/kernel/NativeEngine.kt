package com.ronin.kernel

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.ByteBuffer

class NativeEngine {

    companion object {
        private const val TAG = "RoninNativeEngine"

        init {
            try {
                System.loadLibrary("ronin_kernel")
                Log.i(TAG, "Ronin Kernel Native Library loaded successfully.")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Failed to load ronin_kernel library: ${e.message}")
            }
        }
    }

    /**
     * Initializes and links kernel components.
     */
    external fun initializeKernel(filesDir: String)

    /**
     * Native call to process intent vectors using NEON SIMD.
     * Uses DirectByteBuffer for zero-copy access.
     */
    private external fun computeSimilarity(bufferA: ByteBuffer, bufferB: ByteBuffer): Float

    /**
     * Maps a DirectByteBuffer directly to the Ronin Adaptive Checkpoint schema.
     */
    private external fun loadCheckpoint(byteBuffer: ByteBuffer): Boolean

    /**
     * Syncs Android lifecycle (0 = Background, 1 = Foreground).
     */
    private external fun updateLifecycleState(state: Int)

    /**
     * Native call to process input string via reasoning spine.
     */
    private external fun processInput(input: String): String

    /**
     * Retrieves chat history from SQLite (Kernel source of truth).
     */
    private external fun getChatHistory(): Array<String>?

    /**
     * Returns the current internal pressure score (0-100).
     */
    external fun getLMKPressure(): Int

    /**
     * Retrieves chat history from SQLite (Kernel source of truth) with pagination.
     */
    private external fun getChatHistory(limit: Int, offset: Int): Array<String>?

    // --- System Health JNI Bridges ---
    external fun updateSystemHealth(temperature: Float, ramUsedGB: Float, ramTotalGB: Float): Boolean
    external fun setEngineInstance()
    external fun injectLocation(lat: Double, lon: Double)

    // --- Hardware Control JNI Callbacks ---
    var executeHardwareAction: ((Int, Boolean) -> Boolean)? = null
    var onKernelMessage: ((String) -> Unit)? = null

    // Called from C++ ExecHandlers
    @Suppress("unused")
    fun triggerHardwareAction(nodeId: Int, state: Boolean): Boolean {
        Log.i(TAG, "Native request: Triggering action for Node $nodeId (State: $state)")
        return executeHardwareAction?.invoke(nodeId, state) ?: false
    }

    // Called from JNI for asynchronous UI updates
    @Suppress("unused")
    fun pushKernelMessage(message: String) {
        Log.i(TAG, "Kernel push: $message")
        onKernelMessage?.invoke(message)
    }

    // --- Coroutine Wrappers ---

    suspend fun getChatHistoryAsync(limit: Int, offset: Int): List<Pair<String, String>> = withContext(Dispatchers.IO) {
        val raw = getChatHistory(limit, offset) ?: return@withContext emptyList<Pair<String, String>>()
        val result = mutableListOf<Pair<String, String>>()
        for (i in 0 until raw.size / 2) {
            result.add(raw[i * 2] to raw[i * 2 + 1])
        }
        result
    }

    suspend fun processIntentAsync(bufferA: ByteBuffer, bufferB: ByteBuffer): Float = 
        withContext(Dispatchers.Default) {
            computeSimilarity(bufferA, bufferB)
        }

    suspend fun syncLifecycle(isForeground: Boolean) = withContext(Dispatchers.Default) {
        val state = if (isForeground) 1 else 0
        updateLifecycleState(state)
    }

    suspend fun processInputAsync(input: String): String = withContext(Dispatchers.Default) {
        processInput(input)
    }
}
