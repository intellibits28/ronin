#include "ronin_jni_context.h"

#include <cstdint>
#include <string>
#include <vector>

#include "jni_utils.h"

using Ronin::Kernel::JNI::ConvertJStringToString;

namespace {

jobjectArray toStringArray(JNIEnv* env, const std::vector<std::string>& values) {
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray array = env->NewObjectArray(values.size(), string_class, nullptr);
    for (size_t i = 0; i < values.size(); ++i) {
        jstring item = env->NewStringUTF(values[i].c_str());
        env->SetObjectArrayElement(array, i, item);
        env->DeleteLocalRef(item);
    }
    env->DeleteLocalRef(string_class);
    return array;
}

} // namespace

extern "C" {

JNIEXPORT jboolean JNICALL native_storeNote(JNIEnv* env, jobject thiz, jstring t, jstring c, jstring tg) {
    return (g_ltm && g_ltm->storeNote(ConvertJStringToString(env, t), ConvertJStringToString(env, c), ConvertJStringToString(env, tg)))
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL native_storeFact(JNIEnv* env, jobject thiz, jstring e, jstring a, jstring v) {
    return (g_ltm && g_ltm->storeFact(ConvertJStringToString(env, e), ConvertJStringToString(env, a), ConvertJStringToString(env, v)))
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT jstring JNICALL native_lookupFact(JNIEnv* env, jobject thiz, jstring e, jstring a) {
    return env->NewStringUTF(g_ltm ? g_ltm->lookupFact(ConvertJStringToString(env, e), ConvertJStringToString(env, a)).c_str() : "");
}

JNIEXPORT jstring JNICALL native_lookupVault(JNIEnv* env, jobject thiz, jstring t) {
    return env->NewStringUTF(g_ltm ? g_ltm->lookupVault(ConvertJStringToString(env, t)).c_str() : "");
}

JNIEXPORT jobjectArray JNICALL native_searchNotes(JNIEnv* env, jobject thiz, jstring q) {
    if (!g_ltm) return nullptr;
    return toStringArray(env, g_ltm->searchNotes(ConvertJStringToString(env, q)));
}

JNIEXPORT jobjectArray JNICALL native_searchEpisodes(JNIEnv* env, jobject thiz, jstring q) {
    if (!g_ltm) return nullptr;
    return toStringArray(env, g_ltm->searchEpisodes(ConvertJStringToString(env, q)));
}

JNIEXPORT jboolean JNICALL native_storeVault(JNIEnv* env, jobject thiz, jstring t, jstring b) {
    return (g_ltm && g_ltm->storeVault(ConvertJStringToString(env, t), ConvertJStringToString(env, b)))
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL native_storePrediction(JNIEnv* env, jobject thiz, jstring g, jstring n, jstring p, jstring a, jfloat e) {
    return (g_ltm && g_ltm->storePrediction(
        ConvertJStringToString(env, g),
        ConvertJStringToString(env, n),
        ConvertJStringToString(env, p),
        ConvertJStringToString(env, a),
        e))
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT void JNICALL native_applyHumanFeedback(JNIEnv* env, jobject thiz, jstring s, jboolean h) {
    if (g_graph_executor) {
        g_graph_executor->getReflectionEngine().applyHumanFeedback(ConvertJStringToString(env, s), h == JNI_TRUE);
    }
}

JNIEXPORT jobjectArray JNICALL native_getChatHistory(JNIEnv* env, jobject thiz, jint l, jint o) {
    if (!g_ltm) return nullptr;
    auto history = g_ltm->getHistory(l, o);
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray array = env->NewObjectArray(history.size() * 2, string_class, nullptr);
    for (size_t i = 0; i < history.size(); ++i) {
        jstring role = env->NewStringUTF(history[i].first.c_str());
        jstring content = env->NewStringUTF(history[i].second.c_str());
        env->SetObjectArrayElement(array, i * 2, role);
        env->SetObjectArrayElement(array, i * 2 + 1, content);
        env->DeleteLocalRef(role);
        env->DeleteLocalRef(content);
    }
    env->DeleteLocalRef(string_class);
    return array;
}

JNIEXPORT jboolean JNICALL native_loadMyanmarDictionary(JNIEnv* env, jobject thiz, jstring p) {
    return (g_ltm && g_ltm->loadSegmenter(ConvertJStringToString(env, p))) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL native_indexFiles(JNIEnv* env, jobject thiz, jobjectArray paths, jobjectArray names, jlongArray dates) {
    if (!g_ltm) return;
    if (!paths || !names || !dates) return;
    int len = env->GetArrayLength(paths);
    if (env->GetArrayLength(names) != len || env->GetArrayLength(dates) != len) return;

    jlong* dates_ptr = env->GetLongArrayElements(dates, nullptr);
    if (!dates_ptr) return;

    for (int i = 0; i < len; ++i) {
        auto j_path = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
        auto j_name = static_cast<jstring>(env->GetObjectArrayElement(names, i));
        std::string path = ConvertJStringToString(env, j_path);
        std::string name = ConvertJStringToString(env, j_name);
        std::string ext;
        size_t dot = path.find_last_of(".");
        if (dot != std::string::npos) ext = path.substr(dot);
        g_ltm->indexFile(name, path, ext, static_cast<uint64_t>(dates_ptr[i]));
        env->DeleteLocalRef(j_path);
        env->DeleteLocalRef(j_name);
    }

    env->ReleaseLongArrayElements(dates, dates_ptr, JNI_ABORT);
}

JNIEXPORT jobjectArray JNICALL native_searchFiles(JNIEnv* env, jobject thiz, jstring q) {
    if (!g_ltm) return nullptr;
    return toStringArray(env, g_ltm->searchFiles(ConvertJStringToString(env, q)));
}

} // extern "C"
