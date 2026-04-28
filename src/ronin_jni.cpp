#include <jni.h>
#include <memory>
#include <string>
#include "ronin_jni.h"
#include "jni_utils.h"
#include "ronin_kernel.hpp"
#include "intent_engine.h"
#include "memory_manager.h"
#include "long_term_memory.h"
#include "checkpoint_engine.h"
#include "capability_graph.h"
#include "graph_storage.h"
#include "graph_executor.h"
#include "capabilities/file_search_node.h"
#include "capabilities/file_scanner.h"
#include "capabilities/neural_embedding_node.h"
#include "capabilities/hardware_bridge.h"
#include "capabilities/hardware_nodes.h"
#include "checkpoint_manager.h"
#include "lora_engine.h"
#include "ronin_log.h"

#define TAG "RoninKernel_JNI"

using namespace ronin::jni;
using namespace Ronin::Kernel;

static JavaVM* g_vm = nullptr;
static std::unique_ptr<RoninKernel> g_kernel;
static std::unique_ptr<Ronin::Kernel::Intent::IntentEngine> g_intent_engine;

// Forward declarations for processors
namespace {
Ronin::Kernel::CognitiveIntent defaultIntentProcessor(const Ronin::Kernel::Input &input) {
  if (g_intent_engine) {
      std::string s(input.data, input.length);
      return g_intent_engine->process(s, "");
  }
  return {1, 0.5f, true};
}

Ronin::Kernel::Result defaultExecProcessor(uint32_t nodeId, const Ronin::Kernel::CognitiveState &state) {
  LOGI(TAG, "Executing Node %u", nodeId);
  return {true, 0};
}

class JniCapabilityManager : public Ronin::Kernel::CapabilityManager {
public:
  bool canExecute(uint32_t nodeId) const override { return true; }
};

static JniCapabilityManager s_cap_manager;
static Ronin::Kernel::HandlerRegistry s_handler_registry = {
    defaultIntentProcessor, defaultExecProcessor};
}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setEngineInstance(JNIEnv *env, jobject thiz) {
    Ronin::Kernel::Capability::HardwareBridge::initialize(g_vm, env->NewGlobalRef(thiz));
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_initializeKernel(JNIEnv *env, jobject thiz, jstring files_dir) {
    std::string base_path = JStringToStdString(env, files_dir);
    LOGI(TAG, "Initializing Kernel at: %s", base_path.c_str());

    // Reset components
    g_intent_engine = std::make_unique<Ronin::Kernel::Intent::IntentEngine>();
    
    auto ltm = std::make_unique<LongTermMemory>(base_path + "/ronin_l3.db");
    auto mm = std::make_unique<MemoryManager>(20);
    mm->setLongTermMemory(ltm.get());
    
    g_intent_engine->setMemoryManager(mm.get());
    
    // Setup model paths
    std::string models_dir = base_path + "/assets/models/";
    std::string router_path = models_dir + "model.onnx";
    
    auto inference_engine = std::make_unique<Ronin::Kernel::Model::InferenceEngine>(router_path);
    if (!inference_engine->loadRouterModel(router_path)) {
        LOGE(TAG, "Failed to load ONNX Router.");
    }
    
    g_intent_engine->setInferenceEngine(std::move(inference_engine));
    
    // Register Hardware Skills
    g_intent_engine->registerSkill(4, std::make_shared<FlashlightNode>());
    g_intent_engine->registerSkill(5, std::make_shared<LocationNode>());
    g_intent_engine->registerSkill(6, std::make_shared<WifiNode>());
    g_intent_engine->registerSkill(7, std::make_shared<BluetoothNode>());

    g_kernel = std::make_unique<RoninKernel>(s_handler_registry, s_cap_manager);
    LOGI(TAG, "Kernel Initialization Complete.");
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_loadModel(JNIEnv *env, jobject thiz, jstring path) {
    std::string model_path = JStringToStdString(env, path);
    if (!g_intent_engine) return JNI_FALSE;
    
    auto inference = g_intent_engine->getInferenceEngine();
    if (!inference) return JNI_FALSE;
    
    LOGI(TAG, "Hydrating LiteRT-LM from: %s", model_path.c_str());
    return inference->loadModel(model_path) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_processInput(JNIEnv *env, jobject thiz, jstring input) {
    std::string input_str = JStringToStdString(env, input);
    if (!g_kernel || !g_intent_engine) return StdStringToJString(env, "Error: Kernel not initialized.");

    // Process via Kernel Spine
    Input kernel_input = {};
    size_t len = std::min(input_str.length(), sizeof(kernel_input.data) - 1);
    memcpy(kernel_input.data, input_str.c_str(), len);
    kernel_input.length = len;

    g_kernel->tick(kernel_input);
    auto intent = g_kernel->getLastIntent();

    if (intent.id > 1) {
        return StdStringToJString(env, g_intent_engine->executeSkill(intent.id, input_str));
    }

    // Default Reasoning Path
    auto inference = g_intent_engine->getInferenceEngine();
    if (inference && inference->isLoaded()) {
        return StdStringToJString(env, inference->runLiteRTReasoning(input_str));
    }

    // Cloud Fallback
    std::string provider = g_intent_engine->getPrimaryCloudProvider();
    std::string apiKey = Ronin::Kernel::Capability::HardwareBridge::getCloudApiKey(provider);
    return StdStringToJString(env, inference->escalateToCloud(input_str, apiKey, provider));
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_isLoaded(JNIEnv *env, jobject thiz) {
    if (g_intent_engine) {
        auto inference = g_intent_engine->getInferenceEngine();
        return (inference && inference->isLoaded()) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_notifyTrimMemory(JNIEnv *env, jobject thiz, jint level) {
    // Memory management logic
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setOfflineMode(JNIEnv *env, jobject thiz, jboolean offline) {
    if (g_intent_engine) g_intent_engine->setOfflineMode(offline == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setPrimaryCloudProvider(JNIEnv *env, jobject thiz, jstring provider) {
    if (g_intent_engine) {
        g_intent_engine->setPrimaryCloudProvider(JStringToStdString(env, provider));
    }
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) {
    if (g_kernel) g_kernel->injectLocation(lat, lon);
}

} // extern "C"
