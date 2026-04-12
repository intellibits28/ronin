#include "ronin_jni.h"
#include "intent_engine.h"
#include "ronin_log.h"
#include <cstdint>

#define TAG "RoninNativeEngine"

using namespace Ronin::Kernel::Intent;

extern "C" {

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

} // extern "C"
