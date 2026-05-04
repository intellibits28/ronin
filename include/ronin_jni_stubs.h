#pragma once

#ifdef __ANDROID__
#include <jni.h>
#else
// Stub definitions for JNI types to allow host-side header inclusion
// These allow the code to compile on host (Linux x64) where jni.h is missing.
typedef void JNIEnv;
typedef void JavaVM;
typedef void* jobject;
typedef void* jclass;
typedef void* jstring;
typedef void* jmethodID;
typedef void* jobjectArray;
typedef int jint;
typedef bool jboolean;
typedef float jfloat;
typedef double jdouble;
typedef long jlong;

#define JNI_VERSION_1_6 0x00010006
#define JNI_EDETACHED   (-2)

#ifndef JNIEXPORT
#define JNIEXPORT
#endif
#ifndef JNICALL
#define JNICALL
#endif
#endif
