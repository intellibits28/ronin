#pragma once

#include <jni.h>

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

} // extern "C"
