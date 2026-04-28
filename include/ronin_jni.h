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
 * Sets the engine instance for callbacks.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setEngineInstance(
    JNIEnv *env, jobject thiz);

/**
 * Processes input via the reasoning spine.
 */
JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_processInput(
    JNIEnv *env, jobject thiz, jstring input);

/**
 * Dynamically reloads the reasoning model.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_loadModel(
    JNIEnv *env, jobject thiz, jstring path);

/**
 * Checks if the LiteRT-LM model is loaded.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_isLoaded(
    JNIEnv *env, jobject thiz);

/**
 * Returns the absolute path of the currently loaded model.
 */
JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_getActiveModelPath(
    JNIEnv *env, jobject thiz);

/**
 * Notifies the kernel of OS memory pressure.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_notifyTrimMemory(
    JNIEnv *env, jobject thiz, jint level);

/**
 * Injects GPS coordinates into the kernel.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_injectLocation(
    JNIEnv *env, jobject thiz, jdouble lat, jdouble lon);

/**
 * Reports real-time system metrics.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateSystemHealth(
    JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total);

/**
 * Toggles whether cloud escalation is allowed.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setOfflineMode(
    JNIEnv *env, jobject thiz, jboolean offline);

/**
 * Sets the primary cloud provider.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setPrimaryCloudProvider(
    JNIEnv *env, jobject thiz, jstring provider);

/**
 * Returns the current internal pressure score (0-100).
 */
JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_getLMKPressure(
    JNIEnv *env, jobject thiz);

/**
 * Updates the local model registry.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateModelRegistry(
    JNIEnv *env, jobject thiz, jstring json);

/**
 * Updates the Cloud Provider manifest.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateCloudProviders(
    JNIEnv *env, jobject thiz, jstring json);

/**
 * Retrieves chat history from SQLite with pagination.
 */
JNIEXPORT jobjectArray JNICALL
Java_com_ronin_kernel_NativeEngine_getChatHistory(
    JNIEnv *env, jobject thiz, jint limit, jint offset);

} // extern "C"
