#pragma once

#ifdef __ANDROID__
#include <jni.h>
#else
// Stub definitions for JNI types to allow host-side header inclusion
typedef void* JNIEnv;
typedef void* jobject;
typedef void* jclass;
typedef void* jstring;
typedef void* jmethodID;
typedef void* jobjectArray;
typedef void* JavaVM;
typedef int jint;
typedef bool jboolean;
typedef float jfloat;
typedef double jdouble;
typedef long jlong;

#ifndef JNIEXPORT
#define JNIEXPORT
#endif
#ifndef JNICALL
#define JNICALL
#endif
#endif
