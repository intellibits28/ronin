#include "ronin_jni.h"
#include <memory>
#include <string>
#include <cstring>
#include <fstream>
#include <algorithm>
#include "ronin_jni.h"
#include "jni_utils.h"
#include "hal/shared_memory_bridge.h"
#include "ronin_kernel.hpp"
#include "intent_engine.h"
#include "models/inference_engine.h"
#include "models/hydration_manager.h"
#include "capabilities/hardware_bridge.h"
#include "capabilities/chat_skill.h"
#include "capabilities/file_search_node.h"
#include "capabilities/neural_embedding_node.h"
#include "capabilities/hardware_nodes.h"
#include "capabilities/file_scanner.h"
#include "memory_manager.h"
#include "long_term_memory.h"
#include "ronin_log.h"

#define TAG "RoninKernel_JNI"

using namespace Ronin::Kernel::JNI;
using namespace Ronin::Kernel;
using namespace Ronin::Kernel::Model;

#ifdef __ANDROID__
// Global state
static JavaVM* g_vm = nullptr;
static std::unique_ptr<RoninKernel> g_kernel;
static std::unique_ptr<Ronin::Kernel::Intent::IntentEngine> g_intent_engine;
static std::unique_ptr<Ronin::Kernel::Memory::MemoryManager> g_memory_manager;
static std::unique_ptr<Ronin::Kernel::Memory::LongTermMemory> g_ltm;
static std::unique_ptr<Ronin::Kernel::Capability::FileScanner> g_file_scanner;
static std::string g_last_input_str;
static std::string g_last_skill_output;
static std::unique_ptr<::Ronin::Kernel::HAL::SharedMemoryBridge<::Ronin::Kernel::HAL::SpineRingBuffer>> g_spine_consumer;

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
    LOGI(TAG, "Initializing Ronin Kernel (Process: %s)...", is_worker ? "Inference Worker" : "Kernel Core");

    g_spine_consumer = std::make_unique<::Ronin::Kernel::HAL::SharedMemoryBridge<::Ronin::Kernel::HAL::SpineRingBuffer>>("spine_stream");
    if (!g_spine_consumer->create(base_path, is_worker == JNI_TRUE)) {
        LOGW(TAG, "JNI: SHM Bridge Error. Mode: %s", is_worker ? "Producer" : "Consumer");
    }

    if (is_worker == JNI_TRUE) return;

    g_ltm = std::make_unique<Ronin::Kernel::Memory::LongTermMemory>(base_path + "/ronin_memory.db");
    g_memory_manager = std::make_unique<Ronin::Kernel::Memory::MemoryManager>(2048);
    g_memory_manager->setLongTermMemory(g_ltm.get());
    g_intent_engine = std::make_unique<Ronin::Kernel::Intent::IntentEngine>(g_ltm.get());
    g_intent_engine->setMemoryManager(g_memory_manager.get());
    g_intent_engine->loadCapabilities(base_path + "/assets/capabilities.json");

    using namespace Ronin::Kernel::Capability;
    // Phase 2.1: Expert Native Path - Dual-model loading (LiteRT + SentencePiece)
    auto neural_node = std::make_shared<NeuralEmbeddingNode>(
        base_path + "/assets/models/multilingual-e5-small.tflite", 
        base_path + "/assets/sentencepiece.bpe.model"
    );
    auto search_node = std::make_shared<FileSearchNode>(g_ltm.get(), neural_node.get());
    
    g_intent_engine->registerSkill(2, search_node);
    g_intent_engine->registerSkill(3, neural_node);
    g_intent_engine->registerSkill(4, std::make_shared<FlashlightNode>());
    g_intent_engine->registerSkill(5, std::make_shared<LocationNode>());
    g_intent_engine->registerSkill(6, std::make_shared<WifiNode>());
    g_intent_engine->registerSkill(7, std::make_shared<BluetoothNode>());
    
    g_file_scanner = std::make_unique<Ronin::Kernel::Capability::FileScanner>(*g_ltm, neural_node.get());
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
            LOGI(TAG, "Native Bridge: Releasing all engine resources.");
            if (g_intent_engine) {
                // Fully unload all skills (munmap happens here)
                for (uint32_t i = 1; i <= 10; ++i) {
                    auto skill = g_intent_engine->getSkill(i);
                    if (skill) skill->unload();
                }
            }
        }
    };
    
    static class JniCapManager : public CapabilityManager {
        bool canExecute(uint32_t id) const override { return true; }
    } cap_manager;

    g_kernel = std::make_unique<RoninKernel>(registry, cap_manager);
    auto engine = std::make_unique<InferenceEngine>("hybrid_mode");
    engine->setLibPath(native_lib_path);
    engine->setBasePath(base_path);
    
    // Phase 9.0: Register Proxy Node for Gemma 4
    g_intent_engine->registerSkill(1, std::make_shared<ChatSkill>(engine.get()));

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
    if (g_intent_engine) g_intent_engine->setPriority(Ronin::Kernel::Capability::SkillPriority::HIGH);
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
    // Phase 4.5 Audit: HardwareBridge might not be synced in worker process.
    // Use HydrationManager's direct /proc/meminfo reading for reliable RAM Guard.
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
    
    // Phase 9.0: Tick the kernel. Skill Registry + Proxy Nodes handle routing and execution.
    g_kernel->tick(in_data);
    
    if (g_last_skill_output.empty()) {
        return ConvertStringToJString(env, "> Task queued.");
    }
    
    return ConvertStringToJString(env, g_last_skill_output);
}
jobject native_pollInferenceStream(JNIEnv *env, jobject thiz) {
    if (!g_spine_consumer) return nullptr;
    auto* rb = g_spine_consumer->get();
    if (!rb) return nullptr;

    ::Ronin::Kernel::HAL::InferencePacket packet;
    if (rb->pop(packet)) {
        // Requirement 4: Ensure we are not returning empty strings accidentally
        if (std::strlen(packet.fragment) == 0 && !packet.is_final) {
            return nullptr; // Skip empty packets unless it's the final signal
        }

        jclass cls = env->FindClass("com/ronin/kernel/InferencePacket");
        if (!cls) return nullptr;
        jmethodID constructor = env->GetMethodID(cls, "<init>", "(ILjava/lang/String;Z)V");
        jstring fragment = ConvertStringToJString(env, packet.fragment);
        return env->NewObject(cls, constructor, (jint)packet.token_id, fragment, (jboolean)packet.is_final);
    }
    return nullptr;
}


jboolean native_pushTokenToSHM(JNIEnv *env, jobject thiz, jstring fragment, jboolean is_final) {
    if (!g_spine_consumer) return JNI_FALSE;
    auto* rb = g_spine_consumer->get();
    if (!rb) return JNI_FALSE;

    std::string text = ConvertJStringToString(env, fragment);
    ::Ronin::Kernel::HAL::InferencePacket packet;
    packet.sequence_id = 0; 
    packet.token_id = 0; 
    packet.confidence = 1.0f; 
    packet.is_final = (is_final == JNI_TRUE);
    std::strncpy(packet.fragment, text.c_str(), sizeof(packet.fragment) - 1);
    packet.fragment[sizeof(packet.fragment) - 1] = '\0';

    // Phase 8.1: Burst Control - Retry with sleep if buffer is full
    int retries = 50; // Up to 50ms wait
    while (!rb->push(packet) && retries-- > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (retries < 0) {
        LOGW(TAG, "SHM Push FAILED: Buffer overflow after 50ms wait.");
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

jboolean native_isLoaded(JNIEnv *env, jobject thiz) { return g_llm_context.initialized ? JNI_TRUE : JNI_FALSE; }
void native_notifyTrimMemory(JNIEnv *env, jobject thiz, jint level) {
    if (level >= 20 && g_llm_context.engine) g_llm_context.engine->purgeKVCache();
    if (g_memory_manager) g_memory_manager->onMemoryPressure();
    if (g_intent_engine) g_intent_engine->notifyTrimMemory(level);
}

jboolean native_isValidModel(JNIEnv *env, jobject thiz, jstring path) {
    std::string path_str = ConvertJStringToString(env, path);
    std::ifstream file(path_str, std::ios::binary);
    if (!file) {
        LOGE(TAG, "Integrity: Cannot open model file at %s", path_str.c_str());
        return JNI_FALSE;
    }

    // Phase 8.1: Mandatory TFLite Magic Byte Check (TFL3) for .tflite only
    // .litertlm (Gemma Bundle) and .bin (Legacy) do not use standard FlatBuffers headers
    if (path_str.length() > 7 && path_str.substr(path_str.length() - 7) == ".tflite") {
        file.seekg(4);
        char magic[4];
        file.read(magic, 4);
        if (file.gcount() < 4) return JNI_FALSE;

        bool isValid = (std::memcmp(magic, "TFL3", 4) == 0);
        if (isValid) {
            LOGI(TAG, "Integrity: Model verified (TFLite format detected).");
        } else {
            LOGW(TAG, "Integrity: Model FAILED magic byte check (expected TFL3).");
        }
        return isValid ? JNI_TRUE : JNI_FALSE;
    }

    LOGI(TAG, "Integrity: Bypassing magic byte check for non-tflite extension.");
    return JNI_TRUE;
}

void native_setSafeMode(JNIEnv *env, jobject thiz, jboolean enabled) {
    if (g_intent_engine) {
        g_intent_engine->setPriority(enabled ? Ronin::Kernel::Capability::SkillPriority::CRITICAL : 
                                              Ronin::Kernel::Capability::SkillPriority::LOW);
        if (enabled) g_intent_engine->stopLowPriorityTasks();
    }
}

jstring native_getActiveModelPath(JNIEnv *env, jobject thiz) {
 return ConvertStringToJString(env, g_llm_context.model_path); }
void native_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) { if (g_kernel) g_kernel->injectLocation(lat, lon); }
jboolean native_updateSystemHealth(JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total) {
    Ronin::Kernel::Capability::HardwareBridge::reportSystemHealth(temp, used, total);
    return JNI_TRUE;
}
void native_setOfflineMode(JNIEnv *env, jobject thiz, jboolean offline) { if (g_intent_engine) g_intent_engine->setOfflineMode(offline == JNI_TRUE); }
void native_setPrimaryCloudProvider(JNIEnv *env, jobject thiz, jstring provider) { if (g_intent_engine) g_intent_engine->setPrimaryCloudProvider(ConvertJStringToString(env, provider)); }
jint native_getLMKPressure(JNIEnv *env, jobject thiz) { return g_memory_manager ? g_memory_manager->getPressureScore() : 0; }
jboolean native_updateModelRegistry(JNIEnv *env, jobject thiz, jstring json) { return g_intent_engine ? (g_intent_engine->updateMetadata(ConvertJStringToString(env, json)) ? JNI_TRUE : JNI_FALSE) : JNI_FALSE; }
jboolean native_updateCloudProviders(JNIEnv *env, jobject thiz, jstring json) { return JNI_TRUE; }

jboolean native_scanSpecificPath(JNIEnv *env, jobject thiz, jstring path) {
    std::string path_str = ConvertJStringToString(env, path);
    if (g_file_scanner) {
        g_file_scanner->startScan(path_str);
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

jfloatArray native_generateEmbedding(JNIEnv *env, jobject thiz, jstring text, jboolean is_query) {
    std::string input = ConvertJStringToString(env, text);
    // Find NeuralEmbeddingNode (Skill ID 3)
    auto skill = g_intent_engine ? g_intent_engine->getSkill(3) : nullptr;
    auto* neural_node = dynamic_cast<Ronin::Kernel::Capability::NeuralEmbeddingNode*>(skill.get());
    
    if (neural_node) {
        auto vec = neural_node->generateEmbedding(input, is_query == JNI_TRUE);
        jfloatArray result = env->NewFloatArray(vec.size());
        env->SetFloatArrayRegion(result, 0, vec.size(), vec.data());
        return result;
    }
    return nullptr;
}

jboolean native_warmMemoryPipeline(JNIEnv *env, jobject thiz) {
    // Warm BGE model (Skill ID 3)
    auto skill = g_intent_engine ? g_intent_engine->getSkill(3) : nullptr;
    auto* neural_node = dynamic_cast<Ronin::Kernel::Capability::NeuralEmbeddingNode*>(skill.get());
    if (neural_node) {
        return neural_node->load() ? JNI_TRUE : JNI_FALSE;
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
    {"pollInferenceStreamNative", "()Lcom/ronin/kernel/InferencePacket;", (void*)native_pollInferenceStream},
    {"pushTokenToSHMNative", "(Ljava/lang/String;Z)Z", (void*)native_pushTokenToSHM},
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
    {"generateEmbeddingNative", "(Ljava/lang/String;Z)[F", (void*)native_generateEmbedding},
    {"isValidModelNative", "(Ljava/lang/String;)Z", (void*)native_isValidModel},
    {"warmMemoryPipelineNative", "()Z", (void*)native_warmMemoryPipeline},
    {"getChatHistoryNative", "(II)[Ljava/lang/String;", (void*)native_getChatHistory}
};

void native_shutdownKernel(JNIEnv *env, jobject thiz) {
    if (g_kernel) {
        g_kernel->shutdown();
    }
}

static JNINativeMethod g_worker_methods[] = {
    {"initializeKernelNative", "(Ljava/lang/String;Ljava/lang/String;Z)V", (void*)native_initializeKernel},
    {"pushTokenToSHMNative", "(Ljava/lang/String;Z)Z", (void*)native_pushTokenToSHM},
    {"getFreeRamGBNative", "()F", (void*)native_getFreeRamGB},
    {"shutdownKernelNative", "()V", (void*)native_shutdownKernel}
};

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_vm = vm;
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    auto register_class = [&](const char* class_name, JNINativeMethod* methods, int count) {
        jclass cls = env->FindClass(class_name);
        if (cls) {
            if (env->RegisterNatives(cls, methods, count) < 0) {
                LOGE(TAG, "JNI: RegisterNatives FAILED for %s. Checking signatures...", class_name);
                // Nuclear Guardrail: Even if one fails, we clear so the process lives
                if (env->ExceptionCheck()) env->ExceptionClear();
            } else {
                LOGI(TAG, "JNI: Successfully registered %d methods for %s", count, class_name);
            }
        } else {
            // This is expected if the library is loaded by a classloader that doesn't see this class
            LOGW(TAG, "JNI: Class %s not found in current context.", class_name);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
    };

    register_class("com/ronin/kernel/NativeEngine", g_methods, sizeof(g_methods) / sizeof(g_methods[0]));
    register_class("com/ronin/kernel/InferenceService", g_worker_methods, sizeof(g_worker_methods) / sizeof(g_worker_methods[0]));

    LOGI(TAG, "Ronin Unified JNI Registered with Nuclear Guardrails.");
    return JNI_VERSION_1_6;
}
#endif // __ANDROID__
