#include <jni.h>
#include <memory>
#include <string>
#include <cstring>
#include <algorithm>
#include "ronin_jni.h"
#include "jni_utils.h"
#include "ronin_kernel.hpp"
#include "intent_engine.h"
#include "models/inference_engine.h"
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

using namespace ronin::jni;
using namespace Ronin::Kernel;
using namespace Ronin::Kernel::Model;

static JavaVM* g_vm = nullptr;
static std::unique_ptr<RoninKernel> g_kernel;
static std::unique_ptr<Ronin::Kernel::Intent::IntentEngine> g_intent_engine;
static std::unique_ptr<Ronin::Kernel::Memory::MemoryManager> g_memory_manager;
static std::unique_ptr<Ronin::Kernel::Memory::LongTermMemory> g_ltm;
static std::unique_ptr<Ronin::Kernel::Capability::FileScanner> g_file_scanner;
static std::string g_last_input_str;
static std::string g_last_skill_output;

// --- Hybrid Reasoning State ---
namespace {

struct LlmEngineContext {
    std::unique_ptr<InferenceEngine> engine;
    std::string model_path;
    bool initialized = false;
};

static LlmEngineContext g_llm_context;

} // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_vm = vm;
    LOGI(TAG, "Ronin JNI Spine Hydrated.");
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setEngineInstance(JNIEnv *env, jobject thiz) {
    Ronin::Kernel::Capability::HardwareBridge::initialize(g_vm, env->NewGlobalRef(thiz));
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_initializeKernel(JNIEnv *env, jobject thiz, jstring files_dir) {
    std::string base_path = ConvertJStringToString(env, files_dir);
    LOGI(TAG, "Initializing Ronin Kernel Core...");

    // 1. Setup Memory Spines (L1/L2/L3)
    g_ltm = std::make_unique<Ronin::Kernel::Memory::LongTermMemory>(base_path + "/ronin_memory.db");
    g_memory_manager = std::make_unique<Ronin::Kernel::Memory::MemoryManager>(2048);
    g_memory_manager->setLongTermMemory(g_ltm.get());

    // 2. Setup Intent Engine
    g_intent_engine = std::make_unique<Ronin::Kernel::Intent::IntentEngine>();
    g_intent_engine->setMemoryManager(g_memory_manager.get());
    
    // Phase 5.0: Load Dynamic Manifest from synchronized assets
    std::string manifest_path = base_path + "/assets/capabilities.json";
    g_intent_engine->loadCapabilities(manifest_path);

    // 3. Register Modular Skills with full dependency injection
    using namespace Ronin::Kernel::Capability;
    
    auto neural_node = std::make_shared<NeuralEmbeddingNode>(base_path + "/assets/models/bge_base.onnx");
    auto search_node = std::make_shared<FileSearchNode>(g_ltm.get(), neural_node.get());
    
    g_intent_engine->registerSkill(1, std::make_shared<ChatSkill>());
    g_intent_engine->registerSkill(2, search_node);
    g_intent_engine->registerSkill(3, neural_node);
    g_intent_engine->registerSkill(4, std::make_shared<FlashlightNode>());
    g_intent_engine->registerSkill(5, std::make_shared<LocationNode>());
    g_intent_engine->registerSkill(6, std::make_shared<WifiNode>());
    g_intent_engine->registerSkill(7, std::make_shared<BluetoothNode>());
    
    // 4. Setup Background File Scanner
    g_file_scanner = std::make_unique<Ronin::Kernel::Capability::FileScanner>(*g_ltm, neural_node.get());
    g_file_scanner->setDatabaseReady(true);
    g_file_scanner->startScan("/storage/emulated/0/"); 

    // 5. Initialize Kernel Spine
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
        }
    };
    
    static class JniCapManager : public CapabilityManager {
        bool canExecute(uint32_t id) const override { return true; }
    } cap_manager;

    g_kernel = std::make_unique<RoninKernel>(registry, cap_manager);
    
    // Ensure inference engine wrapper is ready for hybrid calls
    g_llm_context.engine = std::make_unique<InferenceEngine>("hybrid_mode");
    
    LOGI(TAG, "Ronin Kernel Core Active.");
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_notifyModelLoaded(JNIEnv *env, jobject thiz, jstring path) {
    std::string model_path = ConvertJStringToString(env, path);
    LOGI(TAG, "C++ Kernel Notified: Hybrid Model Ready at %s", model_path.c_str());
    g_llm_context.initialized = true;
    g_llm_context.model_path = model_path;
}

JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_processInput(JNIEnv *env, jobject thiz, jstring input) {
    std::string input_str = ConvertJStringToString(env, input);
    g_last_input_str = input_str; 
    g_last_skill_output.clear();
    
    if (!g_kernel) {
        return ConvertStringToJString(env, "Error: Ronin Kernel not initialized.");
    }

    // Phase 4.0: Unified Routing Spine (Rule 1)
    Ronin::Kernel::Input in_data = {};
    std::strncpy(in_data.data, input_str.c_str(), sizeof(in_data.data) - 1);
    in_data.length = std::min(input_str.length(), (size_t)(sizeof(in_data.data) - 1));
    
    g_kernel->tick(in_data);
    
    auto lastIntent = g_kernel->getLastIntent();
    
    // Command fast-path (ID 0)
    if (input_str.starts_with("/") || lastIntent.id == 0) {
        return ConvertStringToJString(env, g_last_skill_output.empty() ? "> Command Processed." : g_last_skill_output);
    }

    // Skill execution fast-path (ID > 1)
    if (lastIntent.id > 1) {
        return ConvertStringToJString(env, g_last_skill_output);
    }

    // Direct Neural Reasoning Path (ID 1 or fallback)
    std::string response = "";
    if (g_llm_context.initialized && g_llm_context.engine) {
        response = g_llm_context.engine->runLiteRTReasoning(input_str);
    }

    if (response.empty()) {
        std::string provider = "Gemini"; 
        if (g_intent_engine) {
            provider = g_intent_engine->getPrimaryCloudProvider();
        }
        
        std::string apiKey = Ronin::Kernel::Capability::HardwareBridge::getCloudApiKey(provider);

        if (!apiKey.empty()) {
            LOGI(TAG, "Attempting Cloud Fallback for Reasoning (Provider: %s)...", provider.c_str());
            return ConvertStringToJString(env, g_llm_context.engine->escalateToCloud(input_str, apiKey, provider));
        } else {
            return ConvertStringToJString(env, "Error: Reasoning spine not hydrated and Cloud API Key missing for " + provider);
        }
    }

    return ConvertStringToJString(env, response);
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_isLoaded(JNIEnv *env, jobject thiz) {
    return g_llm_context.initialized ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_notifyTrimMemory(JNIEnv *env, jobject thiz, jint level) {
    LOGI(TAG, "Memory Trim Requested: Level %d", level);
    if (level >= 20) { 
        if (g_llm_context.engine) {
            g_llm_context.engine->purgeKVCache();
        }
    }
    if (g_memory_manager) {
        g_memory_manager->onMemoryPressure();
    }
}

JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_getActiveModelPath(JNIEnv *env, jobject thiz) {
    return ConvertStringToJString(env, g_llm_context.model_path);
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) {
    if (g_kernel) g_kernel->injectLocation(lat, lon);
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateSystemHealth(JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total) {
    LOGD(TAG, "System Health: Temp=%.1f, RAM=%.1f/%.1f", temp, used, total);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setOfflineMode(JNIEnv *env, jobject thiz, jboolean offline) {
    if (g_intent_engine) g_intent_engine->setOfflineMode(offline == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setPrimaryCloudProvider(JNIEnv *env, jobject thiz, jstring provider) {
    if (g_intent_engine) {
        g_intent_engine->setPrimaryCloudProvider(ConvertJStringToString(env, provider));
    }
}

JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_getLMKPressure(JNIEnv *env, jobject thiz) {
    if (g_memory_manager) return g_memory_manager->getPressureScore();
    return 0;
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateModelRegistry(JNIEnv *env, jobject thiz, jstring json) {
    if (g_intent_engine) return g_intent_engine->updateMetadata(ConvertJStringToString(env, json));
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateCloudProviders(JNIEnv *env, jobject thiz, jstring json) {
    return JNI_TRUE;
}

JNIEXPORT jobjectArray JNICALL
Java_com_ronin_kernel_NativeEngine_getChatHistory(JNIEnv *env, jobject thiz, jint limit, jint offset) {
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

} // extern "C"
