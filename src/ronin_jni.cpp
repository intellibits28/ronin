#include "ronin_jni.h"
#include "intent_engine.h"
#include "memory_manager.h"
#include "ronin_log.h"
#include "checkpoint_schema_generated.h"
#include <cstdint>

#define TAG "RoninNativeEngine"

using namespace Ronin::Kernel::Intent;
using namespace Ronin::Kernel::Memory;

// Static kernel components for the JNI bridge
static MemoryManager g_memory_manager(20); // 20 tokens recent window

extern "C" {

/**
 * Maps a DirectByteBuffer directly to the Ronin Adaptive Checkpoint schema.
 * Zero-copy: Buffer is managed in Kotlin/Java and mapped to C++ memory.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_loadCheckpoint(JNIEnv *env, jobject thiz, jobject byte_buffer) {
    if (byte_buffer == nullptr) {
        LOGE(TAG, "loadCheckpoint: byte_buffer is null");
        return JNI_FALSE;
    }

    void *buffer_ptr = env->GetDirectBufferAddress(byte_buffer);
    jlong capacity = env->GetDirectBufferCapacity(byte_buffer);

    if (buffer_ptr == nullptr || capacity <= 0) {
        LOGE(TAG, "loadCheckpoint: Failed to map DirectByteBuffer or capacity is 0");
        return JNI_FALSE;
    }

    // Verify FlatBuffers buffer
    auto verifier = flatbuffers::Verifier(static_cast<const uint8_t*>(buffer_ptr), static_cast<size_t>(capacity));
    if (!Ronin::Kernel::Checkpoint::VerifyCheckpointBuffer(verifier)) {
        LOGE(TAG, "loadCheckpoint: FlatBuffers verification failed (corrupt or misaligned)");
        return JNI_FALSE;
    }

    // Zero-copy access
    auto checkpoint = Ronin::Kernel::Checkpoint::GetCheckpoint(buffer_ptr);

    LOGI(TAG, "Checkpoint loaded via JNI: Frontier=%llu",
         static_cast<unsigned long long>(checkpoint->edge_frontier()));

    return JNI_TRUE;
}

/**
 * Syncs Android lifecycle (Foreground/Background) with the Ronin thermal monitor.
 * Lifecycle State constants: 0 = BACKGROUND, 1 = FOREGROUND.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_updateLifecycleState(
    JNIEnv *env, jobject thiz, jint lifecycle_state) {
    
    // Simple state synchronization: BACKGROUND (0) -> SEVERE to throttle, 
    // FOREGROUND (1) -> NORMAL (actual thermal throttling will adjust this later)
    if (lifecycle_state == 0) {
        LOGI(TAG, "App in Background: Throttling intent engine to SEVERE.");
        g_thermal_state = ThermalState::SEVERE;
    } else {
        LOGI(TAG, "App in Foreground: Resetting intent engine to NORMAL.");
        g_thermal_state = ThermalState::NORMAL;
    }
}

/**
 * Calculates similarity between two pre-normalized 128-dim INT8 vectors.
 * Uses DirectByteBuffer for zero-copy memory access.
 * 
 * Safety Features:
 * 1. Pointer Validation: Checks for NULL from GetDirectBufferAddress.
 * 2. Memory Alignment: Validates 16-byte alignment for optimal NEON vdotq_s32 performance.
 * 3. Error Codes: Returns -1.0f on failure for Kotlin-side handling.
 */
JNIEXPORT jfloat JNICALL
Java_com_ronin_kernel_NativeEngine_computeSimilarity(
    JNIEnv *env, jobject thiz, jobject buffer_a, jobject buffer_b) {
    
    if (buffer_a == nullptr || buffer_b == nullptr) {
        LOGE(TAG, "computeSimilarity: Input buffers are null.");
        return -1.0f; // Error code for Kotlin
    }

    // 1. Pointer Validation (Zero-Copy Memory Access)
    void* ptr_a = env->GetDirectBufferAddress(buffer_a);
    void* ptr_b = env->GetDirectBufferAddress(buffer_b);

    if (ptr_a == nullptr || ptr_b == nullptr) {
        LOGE(TAG, "computeSimilarity: GetDirectBufferAddress returned NULL pointer.");
        return -1.0f;
    }

    jlong cap_a = env->GetDirectBufferCapacity(buffer_a);
    jlong cap_b = env->GetDirectBufferCapacity(buffer_b);

    if (cap_a < 128 || cap_b < 128) {
        LOGE(TAG, "computeSimilarity: Insufficient buffer capacity (min 128 bytes).");
        return -1.0f;
    }

    // 2. Memory Alignment Check (16-byte alignment for NEON optimization)
    uintptr_t addr_a = reinterpret_cast<uintptr_t>(ptr_a);
    uintptr_t addr_b = reinterpret_cast<uintptr_t>(ptr_b);

    if ((addr_a & 15) != 0 || (addr_b & 15) != 0) {
        LOGE(TAG, "computeSimilarity: Buffers are not 16-byte aligned. Performance will degrade.");
        // We could still run the kernel, but for Ronin, we enforce alignment for predictability.
        return -1.0f;
    }

    // 3. Exception Safety: Wrap execution and return -1.0f on internal errors
    try {
        float similarity = compute_intent_similarity_neon(
            static_cast<const int8_t*>(ptr_a), 
            static_cast<const int8_t*>(ptr_b)
        );
        return static_cast<jfloat>(similarity);
    } catch (...) {
        LOGE(TAG, "computeSimilarity: Unknown internal C++ exception occurred.");
        return -1.0f;
    }
}

/**
 * Processes input via the memory manager.
 * Uses DirectByteBuffer for zero-copy access.
 */
JNIEXPORT jfloat JNICALL
Java_com_ronin_kernel_NativeEngine_processInput(JNIEnv *env, jobject thiz, jobject input) {
    if (input == nullptr) return 0.0f;
    
    void* ptr = env->GetDirectBufferAddress(input);
    if (ptr == nullptr) return 0.0f;

    LOGI(TAG, "processInput: Input mapped via DirectByteBuffer.");
    
    // Simulate adding a token to the memory manager
    Token t = {1, 0.9f, {0.1f, 0.2f}}; 
    g_memory_manager.addRecentToken(t);

    return 1.0f;
}

/**
 * Returns the current internal pressure score (0-100) from the MemoryManager.
 */
JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_getLMKPressure(JNIEnv *env, jobject thiz) {
    return static_cast<jint>(g_memory_manager.getPressureScore());
}

} // extern "C"
