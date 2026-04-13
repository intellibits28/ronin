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
#include "ronin_log.h"
#include "checkpoint_schema_generated.h"
#include <cstdint>
#include <memory>
#include <string>

#define TAG "RoninNativeEngine"

using namespace Ronin::Kernel;
using namespace Ronin::Kernel::Intent;
using namespace Ronin::Kernel::Memory;
using namespace Ronin::Kernel::Checkpoint;
using namespace Ronin::Kernel::Reasoning;
using namespace Ronin::Kernel::Capability;

// Use unique_ptr for managed lifecycle of kernel components
static std::unique_ptr<MemoryManager> g_memory_manager;
static std::unique_ptr<LongTermMemory> g_long_term_memory;
static std::unique_ptr<CheckpointEngine> g_checkpoint_engine;
static std::unique_ptr<CapabilityGraph> g_capability_graph;
static std::unique_ptr<GraphStorage> g_graph_storage;
static std::unique_ptr<GraphExecutor> g_graph_executor;
static std::unique_ptr<IntentEngine> g_intent_engine;
static std::unique_ptr<RoninKernel> g_ronin_kernel;
static std::unique_ptr<FileSearchNode> g_file_search_node;
static std::unique_ptr<FileScanner> g_file_scanner;
static std::unique_ptr<NeuralEmbeddingNode> g_neural_embedding_node;

// v3.7 ULTRA-CORE Bridge Implementations
namespace {
class JniCapabilityManager : public Ronin::Kernel::CapabilityManager {
public:
  bool canExecute(uint32_t nodeId) const override {
    // For prototype, all registered nodes are authorized
    return nodeId > 0;
  }
};

Ronin::Kernel::Intent defaultIntentProcessor(const Ronin::Kernel::Input &input) {
  std::string s(input.data, input.length);
  float score = 0.5f;
  if (s.find("search") != std::string::npos) score = 1.0f;
  return {1, score}; // Simple mapping for prototype
}

Ronin::Kernel::Result defaultExecProcessor(uint32_t nodeId, const Ronin::Kernel::CognitiveState &state) {
  LOGI("RoninJNI", "Executing Node %u via Static Dispatch", nodeId);
  return {true, 0};
}

static JniCapabilityManager s_cap_manager;
static Ronin::Kernel::HandlerRegistry s_handler_registry = {
    defaultIntentProcessor, defaultExecProcessor};
} // namespace

extern "C" {

/**
 * Initializes the kernel components with dynamic paths from Android.
 */
JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_initializeKernel(JNIEnv *env, jobject thiz, jstring files_dir) {
    if (files_dir == nullptr) return;
    const char *path_cstr = env->GetStringUTFChars(files_dir, nullptr);
    if (path_cstr == nullptr) return;
    std::string base_path(path_cstr);
    env->ReleaseStringUTFChars(files_dir, path_cstr);

    LOGI(TAG, "Initializing Ronin Kernel at: %s", base_path.c_str());

    // Reset existing instances
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

    // 1. Initialize Memory Components
    g_long_term_memory = std::make_unique<LongTermMemory>(base_path + "/ronin_l3.db");
    g_memory_manager = std::make_unique<MemoryManager>(20);
    g_memory_manager->setLongTermMemory(g_long_term_memory.get());

    // 2. Initialize Checkpoint and File Search
    g_checkpoint_engine = std::make_unique<CheckpointEngine>(base_path + "/checkpoint.bin");
    g_checkpoint_engine->initializeShadowBuffer(1024 * 1024);
    g_neural_embedding_node = std::make_unique<NeuralEmbeddingNode>(base_path + "/models/model.onnx");
    g_file_search_node = std::make_unique<FileSearchNode>(*g_long_term_memory, g_neural_embedding_node.get());
    g_file_scanner = std::make_unique<FileScanner>(*g_long_term_memory, g_neural_embedding_node.get());

    // 3. Initialize Reasoning Spine (Graph)
    g_graph_storage = std::make_unique<GraphStorage>(base_path + "/ronin_graph.db");
    g_capability_graph = std::make_unique<CapabilityGraph>();
    g_graph_storage->loadGraph(*g_capability_graph);
    
    // Default nodes for prototype (Fixed Names for Nuclear Path)
    LOGI(TAG, "Registering mandatory nodes...");
    g_capability_graph->addNode(1, "Reasoning_Engine");
    g_capability_graph->addNode(2, "FileSearchNode");
    g_capability_graph->addNode(3, "NeuralEmbeddingNode");
    g_capability_graph->addEdge(1, 2, 1.0f);
    g_capability_graph->addEdge(1, 3, 0.5f);

    Node* testNode = g_capability_graph->getNodeByID("FileSearchNode");
    if (testNode) {
        LOGI(TAG, "VERIFIED: FileSearchNode (ID %u) added to graph.", testNode->id);
    } else {
        LOGE(TAG, "CRITICAL: FileSearchNode registration FAILED!");
    }

    g_graph_executor = std::make_unique<GraphExecutor>(*g_capability_graph, *g_graph_storage);
    g_intent_engine = std::make_unique<IntentEngine>();
    g_ronin_kernel = std::make_unique<RoninKernel>(s_handler_registry, s_cap_manager);

    // 4. Trigger Background Scan
    LOGI(TAG, "Triggering automatic background scan of /storage/emulated/0");
    g_file_scanner->startScan("/storage/emulated/0");

    LOGI(TAG, "Kernel components synchronized and linked.");
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_loadCheckpoint(JNIEnv *env, jobject thiz, jobject byte_buffer) {
    if (byte_buffer == nullptr) return JNI_FALSE;
    void *buffer_ptr = env->GetDirectBufferAddress(byte_buffer);
    jlong capacity = env->GetDirectBufferCapacity(byte_buffer);
    if (buffer_ptr == nullptr || capacity <= 0) return JNI_FALSE;

    auto verifier = flatbuffers::Verifier(static_cast<const uint8_t*>(buffer_ptr), static_cast<size_t>(capacity));
    if (!Ronin::Kernel::Checkpoint::VerifyCheckpointBuffer(verifier)) {
        LOGE(TAG, "loadCheckpoint: FlatBuffers verification failed.");
        return JNI_FALSE;
    }

    auto checkpoint = Ronin::Kernel::Checkpoint::GetCheckpoint(buffer_ptr);
    LOGI(TAG, "Checkpoint mapped via JNI. Frontier bitmask: %llu", 
         static_cast<unsigned long long>(checkpoint->edge_frontier()));

    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_updateLifecycleState(JNIEnv *env, jobject thiz, jint lifecycle_state) {
    if (lifecycle_state == 0) {
        LOGI(TAG, "App in Background: Triggering LMK-aware flush.");
        g_thermal_state = ThermalState::SEVERE;
        if (g_checkpoint_engine) g_checkpoint_engine->onLMKSignal();
        if (g_graph_executor) g_graph_executor->triggerAsyncSync();
    } else {
        g_thermal_state = ThermalState::NORMAL;
    }
}

JNIEXPORT jfloat JNICALL
Java_com_ronin_kernel_NativeEngine_computeSimilarity(JNIEnv *env, jobject thiz, jobject buffer_a, jobject buffer_b) {
    void* ptr_a = env->GetDirectBufferAddress(buffer_a);
    void* ptr_b = env->GetDirectBufferAddress(buffer_b);
    if (ptr_a == nullptr || ptr_b == nullptr) return -1.0f;

    try {
        return static_cast<jfloat>(compute_intent_similarity_neon(
            static_cast<const int8_t*>(ptr_a), static_cast<const int8_t*>(ptr_b)));
    } catch (...) {
        return -1.0f;
    }
}

JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_processInput(JNIEnv *env, jobject thiz, jstring input) {
    if (input == nullptr || !g_graph_executor) {
        return env->NewStringUTF("Kernel Error: Graph Executor not ready.");
    }

    const char *input_cstr = env->GetStringUTFChars(input, nullptr);
    if (input_cstr == nullptr) return env->NewStringUTF("Kernel Error: Memory allocation failed.");
    std::string input_str(input_cstr);
    env->ReleaseStringUTFChars(input, input_cstr);

    // 0. Trigger Core Heartbeat (v3.7 logic)
    if (g_ronin_kernel) {
        Input minimalist_input = {};
        size_t len = std::min(input_str.length(), sizeof(minimalist_input.data) - 1);
        memcpy(minimalist_input.data, input_str.c_str(), len);
        minimalist_input.length = len;
        g_ronin_kernel->tick(minimalist_input);
    }

    // 1. Routing Decision (Nuclear Path is now inside selectNextNode)
    Node* next_node = g_graph_executor->selectNextNode(input_str);

    if (next_node && (next_node->id == 2 || next_node->id == 3) && g_file_search_node) {
        LOGI(TAG, "Routing to Hybrid FileSearch capability.");
        auto results = g_file_search_node->execute(input_str);
        if (!results.empty()) {
            return env->NewStringUTF(results[0].c_str());
        }
    }

    // Default response if not routed to search or if search results empty
    return env->NewStringUTF("Input processed via Reasoning Spine (No specific capability triggered).");
}

JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_getLMKPressure(JNIEnv *env, jobject thiz) {
    return g_memory_manager ? static_cast<jint>(g_memory_manager->getPressureScore()) : 0;
}

JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_runMaintenance(JNIEnv *env, jobject thiz, jboolean is_charging) {
    return g_long_term_memory ? static_cast<jint>(g_long_term_memory->runMaintenance(is_charging == JNI_TRUE)) : 0;
}

} // extern "C"
