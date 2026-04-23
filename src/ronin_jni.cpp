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

extern "C" {

/**
 * JNI Bridge Repair: loadModelAndHydrate (Connection Test)
 */
JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_MainActivity_loadModelAndHydrate(JNIEnv *env, jobject thiz, jstring model_path) {
    LOGD(TAG, "JNI Bridge: loadModelAndHydrate called.");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_initializeKernel(JNIEnv *env, jobject thiz, jstring files_dir) {
    if (files_dir == nullptr) return;
    const char *path_cstr = env->GetStringUTFChars(files_dir, nullptr);
    if (path_cstr == nullptr) return;
    std::string base_path(path_cstr);
    env->ReleaseStringUTFChars(files_dir, path_cstr);

    LOGI(TAG, "Initializing Ronin Kernel at: %s", base_path.c_str());

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

    g_long_term_memory = std::make_unique<LongTermMemory>(base_path + "/ronin_l3.db");
    g_memory_manager = std::make_unique<MemoryManager>(20);
    g_memory_manager->setLongTermMemory(g_long_term_memory.get());

    g_checkpoint_engine = std::make_unique<CheckpointEngine>(base_path + "/checkpoint.bin");
    g_checkpoint_engine->initializeShadowBuffer(1024 * 1024);
    g_neural_embedding_node = std::make_shared<NeuralEmbeddingNode>(base_path + "/assets/models/model.onnx");
    g_file_search_node = std::make_shared<FileSearchNode>(g_long_term_memory.get(), g_neural_embedding_node.get());
    
    g_file_scanner = std::make_unique<FileScanner>(*g_long_term_memory, g_neural_embedding_node.get());

    g_graph_storage = std::make_unique<GraphStorage>(base_path + "/ronin_graph.db");
    g_capability_graph = std::make_unique<CapabilityGraph>();
    g_graph_storage->loadGraph(*g_capability_graph);
    
    g_capability_graph->addNode(1, "Reasoning_Engine");
    g_capability_graph->addNode(2, "FileSearchNode");
    g_capability_graph->addNode(3, "NeuralEmbeddingNode");
    g_capability_graph->addNode(4, "FlashlightNode");
    g_capability_graph->addNode(5, "LocationNode");
    g_capability_graph->addNode(6, "WiFiNode");
    g_capability_graph->addNode(7, "BluetoothNode");
    g_capability_graph->addEdge(1, 2, 0.5f);
    g_capability_graph->addEdge(1, 3, 0.5f);
    g_capability_graph->addEdge(1, 4, 0.5f);
    g_capability_graph->addEdge(1, 5, 0.5f);
    g_capability_graph->addEdge(1, 6, 0.5f);
    g_capability_graph->addEdge(1, 7, 0.5f);

    g_graph_executor = std::make_unique<GraphExecutor>(*g_capability_graph, *g_graph_storage);
    g_intent_engine = std::make_unique<Ronin::Kernel::Intent::IntentEngine>();
    g_intent_engine->setMemoryManager(g_memory_manager.get());
    g_intent_engine->loadCapabilities(base_path + "/assets/capabilities.json");

    auto checkpoint_manager = std::make_shared<Ronin::Kernel::Checkpoint::CheckpointManager>(base_path + "/survival_core.bin");
    g_intent_engine->setCheckpointManager(checkpoint_manager);
    
    if (g_file_search_node) g_intent_engine->registerSkill(2, g_file_search_node);
    if (g_neural_embedding_node) g_intent_engine->registerSkill(3, g_neural_embedding_node);
    
    auto inference_engine = std::make_unique<Ronin::Kernel::Model::InferenceEngine>(base_path + "/assets/models/model.onnx");
    g_intent_engine->setInferenceEngine(std::move(inference_engine));

    LOGI(TAG, "Kernel components synchronized and linked.");
}

JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_processInput(JNIEnv *env, jobject thiz, jstring input) {
    if (input == nullptr || !g_graph_executor) return env->NewStringUTF("Error: Engine Not Ready.");
    const char *input_cstr = env->GetStringUTFChars(input, nullptr);
    std::string input_str(input_cstr);
    env->ReleaseStringUTFChars(input, input_cstr);

    auto inference = g_intent_engine ? g_intent_engine->getInferenceEngine() : nullptr;
    if (inference) {
        std::string response = inference->runLiteRTReasoning(input_str);
        return env->NewStringUTF(response.c_str());
    }
    return env->NewStringUTF("Error: Inference Spine Failure.");
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
    LOGI(TAG, "NativeEngine_hydrate called.");
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
    LOGI(TAG, "NativeEngine_updateLifecycleState: %d", state);
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
Java_com_ronin_kernel_NativeEngine_getChatHistory__(JNIEnv *env, jobject thiz) {
    jclass stringClass = env->FindClass("java/lang/String");
    return env->NewObjectArray(0, stringClass, nullptr);
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
    LOGI(TAG, "NativeEngine_notifyTrimMemory: %d", level);
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) {
    LOGI(TAG, "NativeEngine_injectLocation: %f, %f", lat, lon);
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
