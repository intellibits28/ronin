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
#include "models/inference_engine.h"
#include "models/hydration_manager.h"
#include "capabilities/hardware_bridge.h"
#include "capabilities/chat_skill.h"
#include "capabilities/file_search_node.h"
#include "capabilities/hardware_nodes.h"
#include "capabilities/file_scanner.h"
#include "memory_manager.h"
#include "long_term_memory.h"
#include "memory_database.h"
#include "capabilities/memory_search_skill.h"
#include "capabilities/archive_memory_skill.h"
#include "ronin_log.h"

#define TAG "RoninKernel_JNI"

using namespace Ronin::Kernel::JNI;
using namespace Ronin::Kernel;
using namespace Ronin::Kernel::Model;

#ifdef __ANDROID__
#include <android/log.h>

// Global state
static JavaVM* g_vm = nullptr;
static std::unique_ptr<RoninKernel> g_kernel;
static std::unique_ptr<Ronin::Kernel::Intent::IntentEngine> g_intent_engine;
static std::unique_ptr<Ronin::Kernel::Memory::MemoryManager> g_memory_manager;
static std::unique_ptr<Ronin::Kernel::Memory::LongTermMemory> g_ltm;
static std::unique_ptr<Ronin::Kernel::Data::MemoryDatabase> g_memory_db;
static std::unique_ptr<Ronin::Kernel::Capability::FileScanner> g_file_scanner;
static std::string g_last_input_str;
static std::string g_last_skill_output;

namespace {
struct LlmEngineContext {
    InferenceEngine* engine = nullptr;
    std::string model_path;
    bool initialized = false;
};
static LlmEngineContext g_llm_context;
}

// --- Native Implementations ---

void native_initializeKernel(JNIEnv *env, jobject thiz, jstring files_dir, jstring lib_dir, jboolean is_worker) {
    std::string base_path = ConvertJStringToString(env, files_dir);
    std::string native_lib_path = ConvertJStringToString(env, lib_dir);
    
    if (is_worker == JNI_TRUE) {
        LOGI(TAG, "Initializing Ronin Inference Worker...");
        return;
    }

    LOGI(TAG, "Initializing Ronin Kernel Core...");

    g_ltm = std::make_unique<Ronin::Kernel::Memory::LongTermMemory>(base_path + "/ronin_cognitive.db");
    g_memory_db = std::make_unique<Ronin::Kernel::Data::MemoryDatabase>(base_path + "/ronin_memory.db");
    g_memory_manager = std::make_unique<Ronin::Kernel::Memory::MemoryManager>(2048);
    g_memory_manager->setLongTermMemory(g_ltm.get());
    g_intent_engine = std::make_unique<Ronin::Kernel::Intent::IntentEngine>(g_ltm.get());
    g_intent_engine->setMemoryManager(g_memory_manager.get());
    g_intent_engine->loadCapabilities(base_path + "/assets/capabilities.json");

    using namespace Ronin::Kernel::Capability;
    auto search_node = std::make_shared<FileSearchNode>(g_ltm.get());
    auto memory_search_skill = std::make_shared<MemorySearchSkill>(g_memory_db.get());
    auto memory_archive_skill = std::make_shared<ArchiveMemorySkill>(g_memory_db.get());
    
    g_intent_engine->registerSkill(2, search_node);
    g_intent_engine->registerSkill(4, std::make_shared<FlashlightNode>());
    g_intent_engine->registerSkill(5, std::make_shared<LocationNode>());
    g_intent_engine->registerSkill(6, std::make_shared<WifiNode>());
    g_intent_engine->registerSkill(7, std::make_shared<BluetoothNode>());
    g_intent_engine->registerSkill(8, memory_search_skill);
    g_intent_engine->registerSkill(9, memory_archive_skill);
    
    g_file_scanner = std::make_unique<Ronin::Kernel::Capability::FileScanner>(*g_ltm);
    g_file_scanner->setDatabaseReady(true);
    g_intent_engine->setLowPriorityStopCallback([]() { if (g_file_scanner) g_file_scanner->stopScan(); });

    static HandlerRegistry registry = {
        [](const Input& in) -> CognitiveIntent {
            if (g_intent_engine) return g_intent_engine->process(std::string(in.data, in.length), "");
            return {1, 0.5f, true};
        },
        [](uint32_t id, const CognitiveState& state) -> Result {
            if (g_intent_engine) {
                g_last_skill_output = g_intent_engine->executeSkill(id, g_last_input_str); 
                return {true, 0};
            }
            return {false, -1};
        },
        []() {
            LOGI(TAG, "Native Bridge: Releasing resources.");
        }
    };
    
    static class JniCapManager : public CapabilityManager {
        bool canExecute(uint32_t id) const override { return true; }
    } cap_manager;

    g_kernel = std::make_unique<RoninKernel>(registry, cap_manager);
    auto engine = std::make_unique<InferenceEngine>("hybrid_mode");
    engine->setLibPath(native_lib_path);
    engine->setBasePath(base_path);
    
    g_intent_engine->registerSkill(1, std::make_shared<ChatSkill>(engine.get(), g_ltm.get()));

    g_llm_context.engine = engine.get();
    g_intent_engine->setInferenceEngine(std::move(engine));
    
    LOGI(TAG, "Ronin Kernel Core Active.");
}

void native_setEngineInstance(JNIEnv *env, jobject thiz) {
    Ronin::Kernel::Capability::HardwareBridge::initialize(g_vm, env->NewGlobalRef(thiz));
}

void native_notifyModelLoaded(JNIEnv *env, jobject thiz, jstring path) {
    std::string model_path = ConvertJStringToString(env, path);
    g_llm_context.initialized = true;
    g_llm_context.model_path = model_path;
    if (g_llm_context.engine) g_llm_context.engine->loadModel(model_path);
}

void native_stopLowPriorityTasks(JNIEnv *env, jobject thiz) {
    if (g_file_scanner) g_file_scanner->stopScan();
}

void native_setPriority(JNIEnv *env, jobject thiz, jint priority) {
    if (g_intent_engine) g_intent_engine->setPriority(static_cast<Ronin::Kernel::Capability::SkillPriority>(priority));
}

jstring native_checkFileAccess(JNIEnv *env, jobject thiz, jstring path) {
    std::string p = ConvertJStringToString(env, path);
    if (access(p.c_str(), R_OK) == 0) return ConvertStringToJString(env, "OK");
    return ConvertStringToJString(env, "ACCESS DENIED");
}

jfloat native_getFreeRamGB(JNIEnv *env, jobject thiz) {
    uint64_t availableBytes = ::Ronin::Kernel::Model::HydrationManager::getAvailableRAM();
    return static_cast<jfloat>(availableBytes) / (1024.0f * 1024.0f * 1024.0f);
}

jstring native_processInput(JNIEnv *env, jobject thiz, jstring input) {
    std::string input_str = ConvertJStringToString(env, input);
    g_last_input_str = input_str; g_last_skill_output.clear();
    if (!g_kernel) return ConvertStringToJString(env, "Error: Kernel not initialized.");

    Ronin::Kernel::Input in_data = {};
    std::strncpy(in_data.data, input_str.c_str(), sizeof(in_data.data) - 1);
    in_data.length = std::min(input_str.length(), (size_t)(sizeof(in_data.data) - 1));
    
    g_kernel->tick(in_data);
    
    return ConvertStringToJString(env, g_last_skill_output);
}

jboolean native_isLoaded(JNIEnv *env, jobject thiz) { return g_llm_context.initialized ? JNI_TRUE : JNI_FALSE; }

void native_notifyTrimMemory(JNIEnv *env, jobject thiz, jint level) {
    if (g_memory_manager) g_memory_manager->onMemoryPressure();
    if (g_intent_engine) g_intent_engine->notifyTrimMemory(level);
}

jboolean native_isValidModel(JNIEnv *env, jobject thiz, jstring path) {
    return JNI_TRUE; 
}

void native_setSafeMode(JNIEnv *env, jobject thiz, jboolean enabled) {
    if (g_intent_engine) {
        g_intent_engine->setPriority(enabled ? Ronin::Kernel::Capability::SkillPriority::CRITICAL : 
                                              Ronin::Kernel::Capability::SkillPriority::LOW);
    }
}

jstring native_getActiveModelPath(JNIEnv *env, jobject thiz) {
    return ConvertStringToJString(env, g_llm_context.model_path); 
}

void native_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) { if (g_kernel) g_kernel->injectLocation(lat, lon); }

jboolean native_updateSystemHealth(JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total) {
    Ronin::Kernel::Capability::HardwareBridge::reportSystemHealth(temp, used, total);
    return JNI_TRUE;
}

void native_setOfflineMode(JNIEnv *env, jobject thiz, jboolean offline) { if (g_intent_engine) g_intent_engine->setOfflineMode(offline == JNI_TRUE); }

void native_setPrimaryCloudProvider(JNIEnv *env, jobject thiz, jstring provider) { if (g_intent_engine) g_intent_engine->setPrimaryCloudProvider(ConvertJStringToString(env, provider)); }

jint native_getLMKPressure(JNIEnv *env, jobject thiz) { return g_memory_manager ? g_memory_manager->getPressureScore() : 0; }

jboolean native_updateModelRegistry(JNIEnv *env, jobject thiz, jstring json) { return JNI_TRUE; }

jboolean native_updateCloudProviders(JNIEnv *env, jobject thiz, jstring json) { return JNI_TRUE; }

jboolean native_scanSpecificPath(JNIEnv *env, jobject thiz, jstring path) {
    std::string path_str = ConvertJStringToString(env, path);
    if (g_file_scanner) {
        g_file_scanner->startScan(path_str);
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

jobjectArray native_getChatHistory(JNIEnv *env, jobject thiz, jint limit, jint offset) {
    jclass stringClass = env->FindClass("java/lang/String");
    if (g_ltm) {
        auto history = g_ltm->getHistory(limit, offset);
        jobjectArray array = env->NewObjectArray(history.size() * 2, stringClass, nullptr);
        for (size_t i = 0; i < history.size(); ++i) {
            env->SetObjectArrayElement(array, i * 2, ConvertStringToJString(env, history[i].first));
            env->SetObjectArrayElement(array, i * 2 + 1, ConvertStringToJString(env, history[i].second));
        }
        return array;
    }
    return env->NewObjectArray(0, stringClass, nullptr);
}

void native_resetContext(JNIEnv *env, jobject thiz) {
    if (g_memory_manager) g_memory_manager->clearContext();
}

void native_requestCancellation(JNIEnv *env, jobject thiz) {
    if (g_llm_context.engine) g_llm_context.engine->requestCancellation();
}

void native_shutdownKernel(JNIEnv *env, jobject thiz) {
    if (g_kernel) g_kernel->shutdown();
}

void native_pushTokenToKernel(JNIEnv *env, jobject thiz, jstring token, jboolean is_final) {
    std::string token_str = ConvertJStringToString(env, token);
    Ronin::Kernel::Capability::HardwareBridge::pushToken(token_str, (bool)is_final);
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
    {"nativeResetContext", "()V", (void*)native_resetContext},
    {"requestCancellationNative", "()V", (void*)native_requestCancellation}
};

static JNINativeMethod g_worker_methods[] = {
    {"initializeKernelNative", "(Ljava/lang/String;Ljava/lang/String;Z)V", (void*)native_initializeKernel},
    {"getFreeRamGBNative", "()F", (void*)native_getFreeRamGB},
    {"pushTokenToKernelNative", "(Ljava/lang/String;Z)V", (void*)native_pushTokenToKernel},
    {"shutdownKernelNative", "()V", (void*)native_shutdownKernel}
};

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_vm = vm;
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    auto register_class = [&](const char* class_name, JNINativeMethod* methods, int count) {
        jclass cls = env->FindClass(class_name);
        if (cls) {
            env->RegisterNatives(cls, methods, count);
        }
    };

    register_class("com/ronin/kernel/NativeEngine", g_methods, sizeof(g_methods) / sizeof(g_methods[0]));
    register_class("com/ronin/kernel/InferenceService", g_worker_methods, sizeof(g_worker_methods) / sizeof(g_worker_methods[0]));

    return JNI_VERSION_1_6;
}
#endif // __ANDROID__
