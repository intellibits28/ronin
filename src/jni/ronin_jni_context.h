#pragma once

#include <jni.h>
#include <memory>

#include "graph_executor.h"
#include "long_term_memory.h"

namespace Ronin::Kernel {
namespace Memory {
class LongTermMemory;
}
namespace Reasoning {
class GraphExecutor;
}
}

extern std::shared_ptr<Ronin::Kernel::Memory::LongTermMemory> g_ltm;
extern std::unique_ptr<Ronin::Kernel::Reasoning::GraphExecutor> g_graph_executor;

extern "C" {

JNIEXPORT jboolean JNICALL native_storeNote(JNIEnv* env, jobject thiz, jstring t, jstring c, jstring tg);
JNIEXPORT jboolean JNICALL native_storeFact(JNIEnv* env, jobject thiz, jstring e, jstring a, jstring v);
JNIEXPORT jstring JNICALL native_lookupFact(JNIEnv* env, jobject thiz, jstring e, jstring a);
JNIEXPORT jstring JNICALL native_lookupVault(JNIEnv* env, jobject thiz, jstring t);
JNIEXPORT jobjectArray JNICALL native_searchNotes(JNIEnv* env, jobject thiz, jstring q);
JNIEXPORT jobjectArray JNICALL native_searchEpisodes(JNIEnv* env, jobject thiz, jstring q);
JNIEXPORT jboolean JNICALL native_storeVault(JNIEnv* env, jobject thiz, jstring t, jstring b);
JNIEXPORT jboolean JNICALL native_storePrediction(JNIEnv* env, jobject thiz, jstring g, jstring n, jstring p, jstring a, jfloat e);
JNIEXPORT void JNICALL native_applyHumanFeedback(JNIEnv* env, jobject thiz, jstring s, jboolean h);
JNIEXPORT jobjectArray JNICALL native_getChatHistory(JNIEnv* env, jobject thiz, jint l, jint o);
JNIEXPORT jboolean JNICALL native_loadMyanmarDictionary(JNIEnv* env, jobject thiz, jstring p);
JNIEXPORT void JNICALL native_indexFiles(JNIEnv* env, jobject thiz, jobjectArray paths, jobjectArray names, jlongArray dates);
JNIEXPORT jobjectArray JNICALL native_searchFiles(JNIEnv* env, jobject thiz, jstring q);

}
