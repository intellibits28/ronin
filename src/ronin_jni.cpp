#include "ronin_jni.h"
#include <memory>
#include <string>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include "jni_utils.h"
#include "ronin_kernel.hpp"
#include "intent_engine.h"
#include "memory_manager.h"
#include "long_term_memory.h"
#include "capabilities/hardware_bridge.h"
#include "capabilities/chat_skill.h"
#include "models/inference_engine.h"
#include "ronin_log.h"

#ifdef __ANDROID__

#define TAG "RoninKernel_JNI"

using namespace Ronin::Kernel;
using namespace Ronin::Kernel::Intent;
using namespace Ronin::Kernel::Memory;
using namespace Ronin::Kernel::Model;
using namespace Ronin::Kernel::Capability;

// Concrete implementation of CapabilityManager to resolve abstract class error
class DefaultCapabilityManager : public CapabilityManager {
public:
    bool canExecute(uint32_t nodeId) const override { return true; }
};

static std::unique_ptr<RoninKernel> g_kernel = nullptr;
static std::shared_ptr<LongTermMemory> g_ltm = nullptr;
static std::shared_ptr<IntentEngine> g_intent_engine = nullptr;
static std::unique_ptr<MemoryManager> g_memory_manager = nullptr;
static JavaVM* g_vm = nullptr;

struct LLMContext {
    InferenceEngine* engine = nullptr;
} g_llm_context;

// --- Helper Functions ---

static std::string ConvertJStringToString(JNIEnv* env, jstring jstr) {
    if (!jstr) return "";
    const char* cstr = env->GetStringUTFChars(jstr, nullptr);
    std::string str(cstr);
    env->ReleaseStringUTFChars(jstr, cstr);
    return str;
}

// --- JNI Implementation ---

void native_initializeKernel(JNIEnv *env, jobject thiz, jstring filesDir, jstring libDir, jboolean isWorker) {
    std::string base_path = ConvertJStringToString(env, filesDir);
    std::string native_lib_path = ConvertJStringToString(env, libDir);

    if (!isWorker) {
        // UI Process Initialization
        g_ltm = std::make_shared<LongTermMemory>(base_path + "/ronin_cognitive.db");
        
        // Fix MemoryManager initialization
        g_memory_manager = std::make_unique<MemoryManager>(2048);
        g_memory_manager->setLongTermMemory(g_ltm.get());
        
        g_intent_engine = std::make_shared<IntentEngine>(g_ltm.get());
        
        HardwareBridge::initialize(g_vm, thiz);
        
        HandlerRegistry registry;
        static DefaultCapabilityManager cap_manager; // Use concrete class
        
        g_kernel = std::make_unique<RoninKernel>(registry, cap_manager);
        auto engine = std::make_unique<InferenceEngine>("hybrid_mode");
        engine->setLibPath(native_lib_path);
        engine->setBasePath(base_path);
        
        g_intent_engine->registerSkill(1, std::make_shared<ChatSkill>(engine.get(), g_ltm.get()));

        g_llm_context.engine = engine.get();
        g_intent_engine->setInferenceEngine(std::move(engine));
        
        LOGI(TAG, "Ronin Kernel UI Core Active.");
    } else {
        LOGI(TAG, "Ronin Kernel Worker Node Active.");
    }
}

void native_setEngineInstance(JNIEnv *env, jobject thiz) {}

void native_notifyModelLoaded(JNIEnv *env, jobject thiz, jstring path) {
    if (g_llm_context.engine) {
        // Use loadModel instead of notifyModelLoaded
        g_llm_context.engine->loadModel(ConvertJStringToString(env, path));
    }
}

void native_stopLowPriorityTasks(JNIEnv *env, jobject thiz) {
    if (g_intent_engine) g_intent_engine->stopLowPriorityTasks();
}

void native_setSafeMode(JNIEnv *env, jobject thiz, jboolean enabled) {}
void native_setPriority(JNIEnv *env, jobject thiz, jint priority) {}

jstring native_checkFileAccess(JNIEnv *env, jobject thiz, jstring path) {
    std::string p = ConvertJStringToString(env, path);
    std::ifstream f(p);
    return env->NewStringUTF(f.good() ? "OK" : "DENIED");
}

jfloat native_getFreeRamGB(JNIEnv *env, jobject thiz) {
    return HardwareBridge::getFreeRamGB();
}

jstring native_processInput(JNIEnv *env, jobject thiz, jstring input) {
    if (!g_intent_engine) return env->NewStringUTF("Error: Engine Not Ready.");
    std::string rawInput = ConvertJStringToString(env, input);
    auto intent = g_intent_engine->process(rawInput, "");
    
    // Fix CognitiveIntent member name (id instead of nodeId)
    std::string result = g_intent_engine->executeSkill(intent.id, rawInput);
    return env->NewStringUTF(result.c_str());
}

jboolean native_isLoaded(JNIEnv *env, jobject thiz) {
    return (g_llm_context.engine && g_llm_context.engine->isLoaded()) ? JNI_TRUE : JNI_FALSE;
}

void native_notifyTrimMemory(JNIEnv *env, jobject thiz, jint level) {
    if (g_intent_engine) g_intent_engine->notifyTrimMemory(level);
}

jstring native_getActiveModelPath(JNIEnv *env, jobject thiz) {
    if (g_llm_context.engine) return env->NewStringUTF(g_llm_context.engine->getModelPath().c_str());
    return env->NewStringUTF("");
}

void native_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) {
    if (g_intent_engine) g_intent_engine->updateLocation(lat, lon);
}

jboolean native_updateSystemHealth(JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total) {
    HardwareBridge::reportSystemHealth(temp, used, total);
    return JNI_TRUE;
}

void native_setOfflineMode(JNIEnv *env, jobject thiz, jboolean offline) {}
void native_setPrimaryCloudProvider(JNIEnv *env, jobject thiz, jstring provider) {}

jint native_getLMKPressure(JNIEnv *env, jobject thiz) {
    return g_memory_manager ? g_memory_manager->getPressureScore() : 0;
}

jboolean native_updateModelRegistry(JNIEnv *env, jobject thiz, jstring json) { return JNI_TRUE; }
jboolean native_updateCloudProviders(JNIEnv *env, jobject thiz, jstring json) { return JNI_TRUE; }
jboolean native_scanSpecificPath(JNIEnv *env, jobject thiz, jstring path) { return JNI_TRUE; }

jboolean native_isValidModel(JNIEnv *env, jobject thiz, jstring path) {
    std::string p = ConvertJStringToString(env, path);
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return JNI_FALSE;
    char header[4];
    size_t read = fread(header, 1, 4, f);
    fclose(f);
    return (read == 4 && memcmp(header, "TFL3", 4) == 0) ? JNI_TRUE : JNI_FALSE;
}

jobjectArray native_getChatHistory(JNIEnv *env, jobject thiz, jint limit, jint offset) {
    if (!g_ltm) return nullptr;
    auto history = g_ltm->getHistory(limit, offset);
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(history.size() * 2, stringClass, nullptr);
    for (size_t i = 0; i < history.size(); ++i) {
        env->SetObjectArrayElement(result, i * 2, env->NewStringUTF(history[i].first.c_str()));
        env->SetObjectArrayElement(result, i * 2 + 1, env->NewStringUTF(history[i].second.c_str()));
    }
    return result;
}

void native_resetContext(JNIEnv *env, jobject thiz) {
    if (g_ltm) g_ltm->clearHistory();
    if (g_memory_manager) g_memory_manager->clearContext();
}

void native_requestCancellation(JNIEnv *env, jobject thiz) {
    if (g_llm_context.engine) g_llm_context.engine->requestCancellation();
}

void native_shutdownKernel(JNIEnv *env, jobject thiz) {
    if (g_kernel) g_kernel->shutdown();
}

// --- JNI Registration ---

static JNINativeMethod g_methods[] = {
    {"initializeKernelNative", "(Ljava/lang/String;Ljava/lang/String;Z)V", (void*)native_initializeKernel},
    {"setEngineInstanceNative", "()V", (void*)native_setEngineInstance},
    {"notifyModelLoadedNative", "(Ljava/lang/String;)V", (void*)native_notifyModelLoaded},
    {"stopLowPriorityTasksNative", "()V", (void*)native_stopLowPriorityTasks},
    {"setSafeModeNative", "(Z)V", (void*)native_setSafeMode},
    {"setPriorityNative", "(I)V", (void*)native_setPriority},
    {"checkFileAccessNative", "(Ljava/lang/String;)Ljava/lang/String;", (void*)native_checkFileAccess},
    {"getFreeRamGBNative", "()F", (void*)native_getFreeRamGB},
    {"processInputNative", "(Ljava/lang/String;)Ljava/lang/String;", (void*)native_processInput},
    {"isLoadedNative", "()Z", (void*)native_isLoaded},
    {"notifyTrimMemoryNative", "(I)V", (void*)native_notifyTrimMemory},
    {"getActiveModelPathNative", "()Ljava/lang/String;", (void*)native_getActiveModelPath},
    {"injectLocationNative", "(DD)V", (void*)native_injectLocation},
    {"updateSystemHealthNative", "(FFF)Z", (void*)native_updateSystemHealth},
    {"setOfflineModeNative", "(Z)V", (void*)native_setOfflineMode},
    {"setPrimaryCloudProviderNative", "(Ljava/lang/String;)V", (void*)native_setPrimaryCloudProvider},
    {"getLMKPressureNative", "()I", (void*)native_getLMKPressure},
    {"updateModelRegistryNative", "(Ljava/lang/String;)Z", (void*)native_updateModelRegistry},
    {"updateCloudProvidersNative", "(Ljava/lang/String;)Z", (void*)native_updateCloudProviders},
    {"scanSpecificPathNative", "(Ljava/lang/String;)Z", (void*)native_scanSpecificPath},
    {"isValidModelNative", "(Ljava/lang/String;)Z", (void*)native_isValidModel},
    {"getChatHistoryNative", "(II)[Ljava/lang/String;", (void*)native_getChatHistory},
    {"resetContextNativeJNI", "()V", (void*)native_resetContext},
    {"requestCancellationNative", "()V", (void*)native_requestCancellation}
};

static JNINativeMethod g_worker_methods[] = {
    {"initializeKernelNative", "(Ljava/lang/String;Ljava/lang/String;Z)V", (void*)native_initializeKernel},
    {"getFreeRamGBNative", "()F", (void*)native_getFreeRamGB},
    {"shutdownKernelNative", "()V", (void*)native_shutdownKernel}
};

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    auto register_class = [&](const char* className, JNINativeMethod* methods, int count) {
        jclass cls = env->FindClass(className);
        if (cls) env->RegisterNatives(cls, methods, count);
    };

    register_class("com/ronin/kernel/NativeEngine", g_methods, sizeof(g_methods) / sizeof(g_methods[0]));
    register_class("com/ronin/kernel/InferenceService", g_worker_methods, sizeof(g_worker_methods) / sizeof(g_worker_methods[0]));

    return JNI_VERSION_1_6;
}

#endif // __ANDROID__
