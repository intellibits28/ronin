#ifdef __ANDROID__
#include "ronin_jni.h"
#include "intent_engine.h"
#include "memory_manager.h"
#include "long_term_memory.h"
#include "checkpoint_engine.h"
#include "capability_graph.h"
#include "graph_storage.h"
#include "graph_executor.h"
#include "ronin_kernel.hpp"
#include "capabilities/file_search_node.h"
#include "capabilities/file_scanner.h"
#include "capabilities/neural_embedding_node.h"
#include "capabilities/hardware_bridge.h"
#include "capabilities/hardware_nodes.h"
#include "checkpoint_manager.h"
#include "lora_engine.h"
#include "ronin_log.h"
#include "checkpoint_schema_generated.h"
#include <cstdint>
#include <memory>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <thread>
#include <future>
#include <chrono>
#include <fstream>
#include <cstdlib>

#define TAG "RoninKernel_CPP"

using namespace Ronin::Kernel;
using namespace Ronin::Kernel::Memory;
using namespace Ronin::Kernel::Checkpoint;
using namespace Ronin::Kernel::Reasoning;
using namespace Ronin::Kernel::Capability;

// Managed kernel instances
static std::unique_ptr<MemoryManager> g_memory_manager;
static std::unique_ptr<LongTermMemory> g_long_term_memory;
static std::unique_ptr<CheckpointEngine> g_checkpoint_engine;
static std::unique_ptr<CapabilityGraph> g_capability_graph;
static std::unique_ptr<GraphStorage> g_graph_storage;
static std::unique_ptr<GraphExecutor> g_graph_executor;
static std::unique_ptr<Ronin::Kernel::Intent::IntentEngine> g_intent_engine;
static std::unique_ptr<RoninKernel> g_ronin_kernel;
static std::shared_ptr<FileSearchNode> g_file_search_node;
static std::unique_ptr<FileScanner> g_file_scanner;
static std::shared_ptr<NeuralEmbeddingNode> g_neural_embedding_node;

static JavaVM* g_vm = nullptr;
static jobject g_engine_instance = nullptr;

namespace {
class JniCapabilityManager : public Ronin::Kernel::CapabilityManager {
public:
  bool canExecute(uint32_t nodeId) const override {
    return nodeId > 0;
  }
};

Ronin::Kernel::CognitiveIntent defaultIntentProcessor(const Ronin::Kernel::Input &input) {
  std::string s(input.data, input.length);
  if (g_intent_engine) {
      std::string context = ""; // Context handling simplified for orchestration sync
      return g_intent_engine->process(s, context);
  }
  return {1, 0.5f, true};
}

Ronin::Kernel::Result defaultExecProcessor(uint32_t nodeId, const Ronin::Kernel::CognitiveState &state) {
  LOGI("RoninJNI", "Executing Node %u", nodeId);
  return {true, 0};
}

static JniCapabilityManager s_cap_manager;
static Ronin::Kernel::HandlerRegistry s_handler_registry = {
    defaultIntentProcessor, defaultExecProcessor};
} // namespace

extern "C" {

/**
 * Phase 4.9.5: Adaptive State Lock
 * Synchronizes the Kotlin state with the C++ Inference Spine.
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_MainActivity_loadModelAndHydrate(JNIEnv *env, jobject thiz, jstring model_path) {
    LOGD(TAG, "JNI Bridge: loadModelAndHydrate sequence initiated.");
    
    if (model_path == nullptr || !g_intent_engine) {
        LOGE(TAG, "CRITICAL ERROR: JNI Bridge not ready.");
        return JNI_FALSE;
    }

    const char *path_cstr = env->GetStringUTFChars(model_path, nullptr);
    std::string path(path_cstr);
    env->ReleaseStringUTFChars(model_path, path_cstr);

    auto inference = g_intent_engine->getInferenceEngine();
    if (!inference) return JNI_FALSE;

    // Load Reasoning Brain (.litertlm / .bin)
    bool brain_success = inference->loadModel(path);
    
    if (brain_success) {
        LOGI(TAG, "SUCCESS: Reasoning Brain hydrated and active.");
        Ronin::Kernel::Capability::HardwareBridge::pushMessage("> Kernel: Reasoning Brain Active.");
        return JNI_TRUE;
    } else {
        LOGE(TAG, "ERROR: Reasoning Brain hydration failed.");
        return JNI_FALSE;
    }
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_initializeKernel(JNIEnv *env, jobject thiz, jstring files_dir) {
    if (files_dir == nullptr) return;
    const char *path_cstr = env->GetStringUTFChars(files_dir, nullptr);
    std::string base_path(path_cstr);
    env->ReleaseStringUTFChars(files_dir, path_cstr);

    LOGI(TAG, "Initializing Ronin Kernel v4.9.5 at: %s", base_path.c_str());

    // Clean start
    g_long_term_memory.reset();
    g_memory_manager.reset();
    g_checkpoint_engine.reset();
    g_graph_storage.reset();
    g_capability_graph.reset();
    g_graph_executor.reset();
    g_intent_engine.reset();
    g_ronin_kernel.reset();
    g_file_search_node.reset();
    if (g_file_scanner) g_file_scanner->stopScan();
    g_file_scanner.reset();
    g_neural_embedding_node.reset();

    // 1. Initialize Memory
    g_long_term_memory = std::make_unique<LongTermMemory>(base_path + "/ronin_l3.db");
    g_memory_manager = std::make_unique<MemoryManager>(20);
    g_memory_manager->setLongTermMemory(g_long_term_memory.get());

    // 2. Unified Path Synchronization: All models in /files/models/
    std::string models_dir = base_path + "/models/";
    std::string router_path = models_dir + "model.onnx";
    
    g_checkpoint_engine = std::make_unique<CheckpointEngine>(base_path + "/checkpoint.bin");
    
    // Core Router Components
    g_neural_embedding_node = std::make_shared<NeuralEmbeddingNode>(router_path);
    g_file_search_node = std::make_shared<FileSearchNode>(g_long_term_memory.get(), g_neural_embedding_node.get());
    g_file_scanner = std::make_unique<FileScanner>(*g_long_term_memory, g_neural_embedding_node.get());

    // 3. Structural Intent Alignment
    g_capability_graph = std::make_unique<CapabilityGraph>();
    g_capability_graph->addNode(1, "Reasoning_Engine"); // Intent 1 -> Node 1
    g_capability_graph->addNode(2, "FileSearchNode");   // Intent 2 -> Node 2
    g_capability_graph->addNode(4, "FlashlightNode");    // Intent 4 -> Node 4
    g_capability_graph->addNode(5, "LocationNode");     // Intent 5 -> Node 5
    g_capability_graph->addNode(6, "WiFiNode");         // Intent 6 -> Node 6
    g_capability_graph->addNode(7, "BluetoothNode");    // Intent 7 -> Node 7

    g_graph_storage = std::make_unique<GraphStorage>(base_path + "/ronin_graph.db");
    g_graph_storage->loadGraph(*g_capability_graph);
    g_graph_executor = std::make_unique<GraphExecutor>(*g_capability_graph, *g_graph_storage);
    
    g_intent_engine = std::make_unique<Ronin::Kernel::Intent::IntentEngine>();
    g_intent_engine->setMemoryManager(g_memory_manager.get());
    
    // Phase 4.9.5: Unified Path for Capabilities
    g_intent_engine->loadCapabilities(base_path + "/assets/capabilities.json");
    
    auto checkpoint_manager = std::make_shared<Ronin::Kernel::Checkpoint::CheckpointManager>(base_path + "/survival_core.bin");
    g_intent_engine->setCheckpointManager(checkpoint_manager);
    
    if (g_file_search_node) g_intent_engine->registerSkill(2, g_file_search_node);
    if (g_neural_embedding_node) g_intent_engine->registerSkill(3, g_neural_embedding_node);
    
    // Phase 4.9.6: Hardware Capability Restoration
    g_intent_engine->registerSkill(4, std::make_shared<FlashlightNode>());
    g_intent_engine->registerSkill(5, std::make_shared<LocationNode>());
    g_intent_engine->registerSkill(6, std::make_shared<WifiNode>());
    g_intent_engine->registerSkill(7, std::make_shared<BluetoothNode>());
    
    // 4. Intent Router Hydration (Vital)
    auto inference_engine = std::make_unique<Ronin::Kernel::Model::InferenceEngine>(router_path);
    bool router_ok = inference_engine->loadRouterModel(router_path);
    
    if (router_ok) {
        LOGI(TAG, "Core Router Active. System operational.");
        Ronin::Kernel::Capability::HardwareBridge::pushMessage("> Kernel: Core Router (ONNX) Hydrated.");
    } else {
        LOGE(TAG, "FATAL: Intent Router hydration failed.");
        Ronin::Kernel::Capability::HardwareBridge::pushMessage("> FATAL: Intent Engine Failure.");
    }

    g_intent_engine->setInferenceEngine(std::move(inference_engine));
    g_ronin_kernel = std::make_unique<RoninKernel>(s_handler_registry, s_cap_manager);

    LOGI(TAG, "Kernel Orchestration Sync Complete.");
}

JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_processInput(JNIEnv *env, jobject thiz, jstring input) {
    if (input == nullptr) return env->NewStringUTF("Error: Null Input");
    const char *input_cstr = env->GetStringUTFChars(input, nullptr);
    std::string input_str(input_cstr);
    env->ReleaseStringUTFChars(input, input_cstr);

    if (!g_intent_engine || !g_intent_engine->getInferenceEngine()) {
        return env->NewStringUTF("> Status: Intent Engine Initializing...");
    }

    auto inference = g_intent_engine->getInferenceEngine();
    
    // Requirement 4: Reasoning Fallback Logic
    Input minimalist_input = {};
    size_t len = std::min(input_str.length(), sizeof(minimalist_input.data) - 1);
    memcpy(minimalist_input.data, input_str.c_str(), len);
    minimalist_input.length = len;

    if (g_ronin_kernel) g_ronin_kernel->tick(minimalist_input);
    CognitiveIntent intent = g_ronin_kernel ? g_ronin_kernel->getLastIntent() : CognitiveIntent{1, 0.5f, true};

    // If ID is a hardware skill, execute regardless of brain hydration
    if (intent.id > 1) {
        return env->NewStringUTF(g_intent_engine->executeSkill(intent.id, input_str).c_str());
    }

    // Chat/Reasoning Path (ID 1)
    if (inference->isLoaded()) {
        return env->NewStringUTF(inference->runLiteRTReasoning(input_str).c_str());
    } else {
        // Check Cloud Fallback
        bool offline = g_intent_engine->isOfflineMode();
        if (!offline) {
            std::string provider = g_intent_engine->getPrimaryCloudProvider();
            std::string apiKey = Ronin::Kernel::Capability::HardwareBridge::getCloudApiKey(provider);
            return env->NewStringUTF(inference->escalateToCloud(input_str, apiKey, provider).c_str());
        }
        return env->NewStringUTF("> Status: Local Reasoning Brain Required for Chat.");
    }
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setEngineInstance(JNIEnv *env, jobject thiz) {
    env->GetJavaVM(&g_vm);
    if (g_engine_instance) env->DeleteGlobalRef(g_engine_instance);
    g_engine_instance = env->NewGlobalRef(thiz);
    HardwareBridge::initialize(g_vm, g_engine_instance);
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setPrimaryCloudProvider(JNIEnv *env, jobject thiz, jstring provider) {
    if (g_intent_engine) {
        const char *provider_str = env->GetStringUTFChars(provider, nullptr);
        g_intent_engine->setPrimaryCloudProvider(provider_str);
        env->ReleaseStringUTFChars(provider, provider_str);
    }
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setOfflineMode(JNIEnv *env, jobject thiz, jboolean offline) {
    if (g_intent_engine) g_intent_engine->setOfflineMode(offline == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_isLoaded(JNIEnv *env, jobject thiz) {
    if (g_intent_engine) {
        auto inference = g_intent_engine->getInferenceEngine();
        return (inference && inference->isLoaded()) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_getActiveModelPath(JNIEnv *env, jobject thiz) {
    if (g_intent_engine) {
        auto inference = g_intent_engine->getInferenceEngine();
        if (inference) return env->NewStringUTF(inference->getModelPath().c_str());
    }
    return env->NewStringUTF("None");
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_hydrate(JNIEnv *env, jobject thiz) {
    if (g_intent_engine) {
        auto cm = g_intent_engine->getCheckpointManager();
        if (cm) cm->initialize();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_loadCheckpoint(JNIEnv *env, jobject thiz, jobject byte_buffer) {
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_updateLifecycleState(JNIEnv *env, jobject thiz, jint state) {
    if (g_intent_engine) {
        auto inference = g_intent_engine->getInferenceEngine();
        if (inference) {
            if (state == 0) inference->suspendNPU();
            else inference->resumeNPU();
        }
    }
}

JNIEXPORT jfloat JNICALL
Java_com_ronin_kernel_NativeEngine_computeSimilarity(JNIEnv *env, jobject thiz, jobject buffer_a, jobject buffer_b) {
    return 1.0f;
}

JNIEXPORT jlong JNICALL
Java_com_ronin_kernel_NativeEngine_verifyModel(JNIEnv *env, jobject thiz) {
    return 100;
}

JNIEXPORT jobjectArray JNICALL
Java_com_ronin_kernel_NativeEngine_getChatHistory__II(JNIEnv *env, jobject thiz, jint limit, jint offset) {
    jclass stringClass = env->FindClass("java/lang/String");
    return env->NewObjectArray(0, stringClass, nullptr);
}

JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_getLMKPressure(JNIEnv *env, jobject thiz) {
    return 0;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_notifyTrimMemory(JNIEnv *env, jobject thiz, jint level) {
    if (g_memory_manager) g_memory_manager->onMemoryPressure();
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) {
    if (g_ronin_kernel) g_ronin_kernel->injectLocation(lat, lon);
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_loadModel(JNIEnv *env, jobject thiz, jstring path) {
    if (path == nullptr || !g_intent_engine) return JNI_FALSE;
    const char *path_cstr = env->GetStringUTFChars(path, nullptr);
    std::string model_path(path_cstr);
    env->ReleaseStringUTFChars(path, path_cstr);
    auto inference = g_intent_engine->getInferenceEngine();
    return inference ? (inference->loadModel(model_path) ? JNI_TRUE : JNI_FALSE) : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateCloudProviders(JNIEnv *env, jobject thiz, jstring json) {
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateSystemHealth(JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total) {
    return JNI_TRUE;
}

} // extern "C"
#endif // __ANDROID__
