#include "ronin_jni_context.h"

#include <cstdint>
#include <string>
#include <vector>

#include "jni_utils.h"

using Ronin::Kernel::JNI::ConvertJStringToString;
using Ronin::Kernel::JNI::runtimeContext;

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
    auto& runtime = runtimeContext();
    return (runtime.ltm && runtime.ltm->storeNote(ConvertJStringToString(env, t), ConvertJStringToString(env, c), ConvertJStringToString(env, tg)))
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL native_storeFact(JNIEnv* env, jobject thiz, jstring e, jstring a, jstring v) {
    auto& runtime = runtimeContext();
    return (runtime.ltm && runtime.ltm->storeFact(ConvertJStringToString(env, e), ConvertJStringToString(env, a), ConvertJStringToString(env, v)))
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL native_storeAuditLog(JNIEnv* env, jobject thiz, jstring a, jstring d) {
    auto& runtime = runtimeContext();
    return (runtime.ltm && runtime.ltm->storeAuditLog(ConvertJStringToString(env, a), ConvertJStringToString(env, d)))
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT jstring JNICALL native_lookupFact(JNIEnv* env, jobject thiz, jstring e, jstring a) {
    auto& runtime = runtimeContext();
    return env->NewStringUTF(runtime.ltm ? runtime.ltm->lookupFact(ConvertJStringToString(env, e), ConvertJStringToString(env, a)).c_str() : "");
}

JNIEXPORT jstring JNICALL native_lookupVault(JNIEnv* env, jobject thiz, jstring t) {
    auto& runtime = runtimeContext();
    return env->NewStringUTF(runtime.ltm ? runtime.ltm->lookupVault(ConvertJStringToString(env, t)).c_str() : "");
}

JNIEXPORT jobjectArray JNICALL native_searchNotes(JNIEnv* env, jobject thiz, jstring q) {
    auto& runtime = runtimeContext();
    if (!runtime.ltm) return nullptr;
    return toStringArray(env, runtime.ltm->searchNotes(ConvertJStringToString(env, q)));
}

JNIEXPORT jobjectArray JNICALL native_searchEpisodes(JNIEnv* env, jobject thiz, jstring q) {
    auto& runtime = runtimeContext();
    if (!runtime.ltm) return nullptr;
    return toStringArray(env, runtime.ltm->searchEpisodes(ConvertJStringToString(env, q)));
}

JNIEXPORT jboolean JNICALL native_storeVault(JNIEnv* env, jobject thiz, jstring t, jstring b) {
    auto& runtime = runtimeContext();
    return (runtime.ltm && runtime.ltm->storeVault(ConvertJStringToString(env, t), ConvertJStringToString(env, b)))
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL native_storePrediction(JNIEnv* env, jobject thiz, jstring g, jstring n, jstring p, jstring a, jfloat e) {
    auto& runtime = runtimeContext();
    return (runtime.ltm && runtime.ltm->storePrediction(
        ConvertJStringToString(env, g),
        ConvertJStringToString(env, n),
        ConvertJStringToString(env, p),
        ConvertJStringToString(env, a),
        e))
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT void JNICALL native_applyHumanFeedback(JNIEnv* env, jobject thiz, jstring s, jboolean h) {
    auto& runtime = runtimeContext();
    if (runtime.graph_executor) {
        runtime.graph_executor->getReflectionEngine().applyHumanFeedback(ConvertJStringToString(env, s), h == JNI_TRUE);
    }
}

JNIEXPORT jboolean JNICALL native_updateBelief(JNIEnv* env, jobject thiz, jstring key, jstring value, jfloat confidence) {
    auto& runtime = runtimeContext();
    if (runtime.graph_executor) {
        runtime.graph_executor->getBeliefState().updateBelief(
            ConvertJStringToString(env, key),
            ConvertJStringToString(env, value),
            confidence
        );
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL native_getChatHistory(JNIEnv* env, jobject thiz, jint l, jint o) {
    auto& runtime = runtimeContext();
    if (!runtime.ltm) return nullptr;
    auto history = runtime.ltm->getHistory(l, o);
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
    auto& runtime = runtimeContext();
    return (runtime.ltm && runtime.ltm->loadSegmenter(ConvertJStringToString(env, p))) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL native_indexFiles(JNIEnv* env, jobject thiz, jobjectArray paths, jobjectArray names, jlongArray dates) {
    auto& runtime = runtimeContext();
    if (!runtime.ltm) return;
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
        runtime.ltm->indexFile(name, path, ext, static_cast<uint64_t>(dates_ptr[i]));
        env->DeleteLocalRef(j_path);
        env->DeleteLocalRef(j_name);
    }

    env->ReleaseLongArrayElements(dates, dates_ptr, JNI_ABORT);
}

JNIEXPORT jobjectArray JNICALL native_searchFiles(JNIEnv* env, jobject thiz, jstring q) {
    auto& runtime = runtimeContext();
    if (!runtime.ltm) return nullptr;
    return toStringArray(env, runtime.ltm->searchFiles(ConvertJStringToString(env, q)));
}

JNIEXPORT jboolean JNICALL native_isDocumentIndexed(JNIEnv* env, jobject thiz, jstring docPath, jstring docHash, jstring modelId) {
    auto& runtime = runtimeContext();
    if (!runtime.ltm) return JNI_FALSE;
    bool indexed = runtime.ltm->isDocumentIndexed(
        ConvertJStringToString(env, docPath),
        ConvertJStringToString(env, docHash),
        ConvertJStringToString(env, modelId)
    );
    return indexed ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL native_storeDocumentChunksAtomic(
    JNIEnv* env, jobject thiz,
    jstring docPath, jstring docHash, jstring modelId,
    jintArray chunkIndices, jobjectArray chunkTexts, jfloatArray flatEmbeddings, jint dimension) {
    auto& runtime = runtimeContext();
    if (!runtime.ltm) return JNI_FALSE;
    if (!chunkIndices || !chunkTexts || !flatEmbeddings || dimension <= 0) return JNI_FALSE;

    jsize numChunks = env->GetArrayLength(chunkIndices);
    if (numChunks <= 0 || env->GetArrayLength(chunkTexts) != numChunks) return JNI_FALSE;
    if (env->GetArrayLength(flatEmbeddings) < numChunks * dimension) return JNI_FALSE;

    jint* idxPtr = env->GetIntArrayElements(chunkIndices, nullptr);
    jfloat* embPtr = env->GetFloatArrayElements(flatEmbeddings, nullptr);
    if (!idxPtr || !embPtr) {
        if (idxPtr) env->ReleaseIntArrayElements(chunkIndices, idxPtr, JNI_ABORT);
        if (embPtr) env->ReleaseFloatArrayElements(flatEmbeddings, embPtr, JNI_ABORT);
        return JNI_FALSE;
    }

    std::vector<LongTermMemory::DocumentChunk> chunks;
    chunks.reserve(numChunks);
    for (jsize i = 0; i < numChunks; ++i) {
        jstring jText = (jstring)env->GetObjectArrayElement(chunkTexts, i);
        std::string text = ConvertJStringToString(env, jText);
        if (jText) env->DeleteLocalRef(jText);

        LongTermMemory::DocumentChunk chunk;
        chunk.index = idxPtr[i];
        chunk.text = std::move(text);
        chunk.embedding.assign(embPtr + i * dimension, embPtr + (i + 1) * dimension);
        chunks.push_back(std::move(chunk));
    }

    env->ReleaseIntArrayElements(chunkIndices, idxPtr, JNI_ABORT);
    env->ReleaseFloatArrayElements(flatEmbeddings, embPtr, JNI_ABORT);

    bool ok = runtime.ltm->storeDocumentChunksAtomic(
        ConvertJStringToString(env, docPath),
        ConvertJStringToString(env, docHash),
        ConvertJStringToString(env, modelId),
        chunks
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL native_storeDocumentEmbedding(JNIEnv* env, jobject thiz, jstring docPath, jint chunkIndex, jstring chunkText, jfloatArray embedding) {
    auto& runtime = runtimeContext();
    if (!runtime.ltm || !embedding) return JNI_FALSE;
    jsize len = env->GetArrayLength(embedding);
    if (len <= 0) return JNI_FALSE;
    jfloat* ptr = env->GetFloatArrayElements(embedding, nullptr);
    if (!ptr) return JNI_FALSE;
    std::vector<float> vec(ptr, ptr + len);
    env->ReleaseFloatArrayElements(embedding, ptr, JNI_ABORT);

    bool ok = runtime.ltm->storeDocumentEmbedding(
        ConvertJStringToString(env, docPath),
        static_cast<int>(chunkIndex),
        ConvertJStringToString(env, chunkText),
        vec
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL native_hybridSearchWithModel(JNIEnv* env, jobject thiz, jstring queryText, jfloatArray queryEmbedding, jstring docPathFilter, jint topK, jstring modelId) {
    auto& runtime = runtimeContext();
    if (!runtime.ltm) return nullptr;

    std::vector<float> vec;
    if (queryEmbedding) {
        jsize len = env->GetArrayLength(queryEmbedding);
        if (len > 0) {
            jfloat* ptr = env->GetFloatArrayElements(queryEmbedding, nullptr);
            if (ptr) {
                vec.assign(ptr, ptr + len);
                env->ReleaseFloatArrayElements(queryEmbedding, ptr, JNI_ABORT);
            }
        }
    }

    std::string mId = modelId ? ConvertJStringToString(env, modelId) : "";
    auto matches = runtime.ltm->hybridSearch(
        ConvertJStringToString(env, queryText),
        vec,
        ConvertJStringToString(env, docPathFilter),
        static_cast<int>(topK),
        mId
    );

    std::vector<std::string> out;
    out.reserve(matches.size());
    for (const auto& m : matches) {
        nlohmann::json j;
        j["doc_path"] = m.doc_path;
        j["chunk_index"] = m.chunk_index;
        j["chunk_text"] = m.chunk_text;
        j["rrf_score"] = m.rrf_score;
        j["cosine_score"] = m.cosine_score;
        j["lexical_hits"] = m.lexical_hits;
        j["fts_bm25_score"] = m.fts_bm25_score;
        j["model_id"] = m.model_id;
        out.push_back(j.dump());
    }
    return toStringArray(env, out);
}

JNIEXPORT jobjectArray JNICALL native_hybridSearch(JNIEnv* env, jobject thiz, jstring queryText, jfloatArray queryEmbedding, jstring docPathFilter, jint topK) {
    return native_hybridSearchWithModel(env, thiz, queryText, queryEmbedding, docPathFilter, topK, nullptr);
}

} // extern "C"
