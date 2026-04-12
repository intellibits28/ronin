#pragma once

#include <jni.h>

extern "C" {

/**
 * JNI wrapper for calculating similarity between two pre-normalized INT8 vectors.
 * Zero-copy: vectors are accessed via DirectByteBuffer.
 */
JNIEXPORT jfloat JNICALL
Java_com_ronin_kernel_NativeEngine_computeSimilarity(
    JNIEnv *env, jobject thiz, jobject buffer_a, jobject buffer_b);

/**
 * Syncs Android lifecycle (Foreground/Background) with the Ronin thermal monitor.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_updateLifecycleState(
    JNIEnv *env, jobject thiz, jint lifecycle_state);

/**
 * Processes input via the memory manager.
 */
JNIEXPORT jfloat JNICALL
Java_com_ronin_kernel_NativeEngine_processInput(
    JNIEnv *env, jobject thiz, jobject input);

/**
 * Returns the current internal pressure score (0-100).
 */
JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_getLMKPressure(
    JNIEnv *env, jobject thiz);

} // extern "C"
