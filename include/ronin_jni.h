#pragma once

#include "ronin_jni_stubs.h"

extern "C" {

/**
 * Initializes and links kernel components.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_initializeKernel(
    JNIEnv *env, jobject thiz, jstring files_dir);

/**
 * JNI wrapper for calculating similarity between two pre-normalized INT8 vectors.
 * Zero-copy: vectors are accessed via DirectByteBuffer.
 */
JNIEXPORT jfloat JNICALL
Java_com_ronin_kernel_NativeEngine_computeSimilarity(
    JNIEnv *env, jobject thiz, jobject buffer_a, jobject buffer_b);

/**
 * Maps a DirectByteBuffer directly to the Ronin Adaptive Checkpoint schema.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_loadCheckpoint(
    JNIEnv *env, jobject thiz, jobject byte_buffer);

/**
 * Syncs Android lifecycle (Foreground/Background) with the Ronin thermal monitor.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_updateLifecycleState(
    JNIEnv *env, jobject thiz, jint lifecycle_state);

/**
 * Processes input via the reasoning spine.
 */
JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_processInput(
    JNIEnv *env, jobject thiz, jstring input);

/**
 * Returns the current internal pressure score (0-100).
 */
JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_getLMKPressure(
    JNIEnv *env, jobject thiz);

/**
 * Triggers Natural Forgetting background maintenance.
 */
JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_runMaintenance(
    JNIEnv *env, jobject thiz, jboolean is_charging);

/**
 * Toggles whether cloud escalation is allowed.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setOfflineMode(
    JNIEnv *env, jobject thiz, jboolean offline);

/**
 * Returns the absolute path of the currently loaded models.
 */
JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_getActiveModelPath(
    JNIEnv *env, jobject thiz);

/**
 * Runs a 1-token benchmark and returns latency in ms.
 */
JNIEXPORT jlong JNICALL
Java_com_ronin_kernel_NativeEngine_verifyModel(
    JNIEnv *env, jobject thiz);

/**
 * Hydrates kernel state from survival core.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_hydrate(
    JNIEnv *env, jobject thiz);

/**
 * Retrieves chat history from SQLite (Kernel source of truth).
 */
JNIEXPORT jobjectArray JNICALL
Java_com_ronin_kernel_NativeEngine_getChatHistory__(
    JNIEnv *env, jobject thiz);

/**
 * Retrieves chat history with pagination.
 */
JNIEXPORT jobjectArray JNICALL
Java_com_ronin_kernel_NativeEngine_getChatHistory__II(
    JNIEnv *env, jobject thiz, jint limit, jint offset);

/**
 * Sets the engine instance for callbacks.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setEngineInstance(
    JNIEnv *env, jobject thiz);

/**
 * Injects GPS coordinates into the kernel.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_injectLocation(
    JNIEnv *env, jobject thiz, jdouble lat, jdouble lon);

/**
 * Notifies the kernel of OS memory pressure.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_notifyTrimMemory(
    JNIEnv *env, jobject thiz, jint level);

/**
 * Dynamically reloads the reasoning model.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_loadModel(
    JNIEnv *env, jobject thiz, jstring path);

/**
 * Updates the Cloud Provider manifest.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateCloudProviders(
    JNIEnv *env, jobject thiz, jstring json);

/**
 * Reports real-time system metrics.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateSystemHealth(
    JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total);

} // extern "C"
