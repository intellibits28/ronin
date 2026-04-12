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

} // extern "C"
