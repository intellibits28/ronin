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
#include <algorithm>
#include <cctype>

#define TAG "RoninNativeEngine"

using namespace Ronin::Kernel;
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
static std::unique_ptr<Ronin::Kernel::Intent::IntentEngine> g_intent_engine;
static std::unique_ptr<RoninKernel> g_ronin_kernel;
static std::unique_ptr<FileSearchNode> g_file_search_node;
static std::unique_ptr<FileScanner> g_file_scanner;
static std::unique_ptr<NeuralEmbeddingNode> g_neural_embedding_node;

// JNI Callback Caching
static JavaVM* g_vm = nullptr;
static jobject g_engine_instance = nullptr;

// v3.9.1-STABLE Bridge Implementations
namespace {
class JniCapabilityManager : public Ronin::Kernel::CapabilityManager {
public:
  bool canExecute(uint32_t nodeId) const override {
    // For prototype, all registered nodes are authorized
    return nodeId > 0;
  }
};

Ronin::Kernel::CognitiveIntent defaultIntentProcessor(const Ronin::Kernel::Input &input) {
  std::string s(input.data, input.length);
  if (g_intent_engine) {
      std::string context = g_ronin_kernel ? g_ronin_kernel->getSuggestedSubject() : "";
      return g_intent_engine->process(s, context);
  }
  return {1, 0.5f, true};
}

Ronin::Kernel::Result defaultExecProcessor(uint32_t nodeId, const Ronin::Kernel::CognitiveState &state) {
  LOGI("RoninJNI", "Executing Node %u via Static Dispatch [v3.9.1-STABLE]", nodeId);
  
  // Hardware Execution via JNI Callback
  if (nodeId >= 4 && nodeId <= 7) {
      if (!g_vm || !g_engine_instance) {
          LOGE("RoninJNI", "Exec Error: JNI Instance not cached.");
          return {false, -1};
      }

      JNIEnv* env = nullptr;
      if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
          return {false, -2};
      }

      jclass cls = env->GetObjectClass(g_engine_instance);
      jmethodID methodCallback = env->GetMethodID(cls, "triggerHardwareAction", "(IZ)Z");
      
      if (methodCallback) {
          jboolean success = env->CallBooleanMethod(g_engine_instance, methodCallback, 
              static_cast<jint>(nodeId),
              static_cast<jboolean>(state.currentIntent.intent_param));
          return {success == JNI_TRUE, 200};
      }
  }

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
    g_capability_graph->addNode(4, "SystemControlNode");
    g_capability_graph->addNode(5, "LocationNode");
    g_capability_graph->addNode(6, "WiFiNode");
    g_capability_graph->addNode(7, "BluetoothNode");
    g_capability_graph->addEdge(1, 2, 0.5f); // File Search (Reset to 0.5)
    g_capability_graph->addEdge(1, 3, 0.5f); // Neural (Reset to 0.5)
    g_capability_graph->addEdge(1, 4, 0.5f); // SystemControl (Reset to 0.5)
    g_capability_graph->addEdge(1, 5, 0.5f); // Location (Reset to 0.5)
    g_capability_graph->addEdge(1, 6, 0.5f); // WiFi
    g_capability_graph->addEdge(1, 7, 0.5f); // Bluetooth

    Node* testNode = g_capability_graph->getNodeByID("FileSearchNode");
    if (testNode) {
        LOGI(TAG, "VERIFIED: FileSearchNode (ID %u) added to graph.", testNode->id);
    } else {
        LOGE(TAG, "CRITICAL: FileSearchNode registration FAILED!");
    }

    g_graph_executor = std::make_unique<GraphExecutor>(*g_capability_graph, *g_graph_storage);
    g_intent_engine = std::make_unique<Ronin::Kernel::Intent::IntentEngine>();
    g_intent_engine->loadCapabilities(base_path + "/assets/capabilities.json");
    
    // Attach ONNX Inference Engine for Tier 3 intent detection
    auto inference_engine = std::make_unique<Ronin::Kernel::Model::InferenceEngine>(base_path + "/assets/models/model.onnx");
    g_intent_engine->setInferenceEngine(std::move(inference_engine));

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
        Ronin::Kernel::Intent::g_thermal_state = Ronin::Kernel::Intent::ThermalState::SEVERE;
        if (g_checkpoint_engine) g_checkpoint_engine->onLMKSignal();
        if (g_graph_executor) g_graph_executor->triggerAsyncSync();
    } else {
        Ronin::Kernel::Intent::g_thermal_state = Ronin::Kernel::Intent::ThermalState::NORMAL;
    }
}

JNIEXPORT jfloat JNICALL
Java_com_ronin_kernel_NativeEngine_computeSimilarity(JNIEnv *env, jobject thiz, jobject buffer_a, jobject buffer_b) {
    void* ptr_a = env->GetDirectBufferAddress(buffer_a);
    void* ptr_b = env->GetDirectBufferAddress(buffer_b);
    if (ptr_a == nullptr || ptr_b == nullptr) return -1.0f;

    try {
        return static_cast<jfloat>(Ronin::Kernel::Intent::compute_intent_similarity_neon(
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

    // 0. Intent Logic Bypass (Greetings First)
    std::string clean_input = input_str;
    std::transform(clean_input.begin(), clean_input.end(), clean_input.begin(), ::tolower);
    if (clean_input == "hi" || clean_input == "hello" || clean_input == "hey" || clean_input == "mingalaba") {
        if (g_memory_manager) g_memory_manager->clearContext();
        std::string response = "ChatNode: Hello! I am Ronin. How can I help you today?";
        if (g_long_term_memory) {
            g_long_term_memory->storeMessage("user", input_str);
            g_long_term_memory->storeMessage("ronin", response);
        }
        return env->NewStringUTF(response.c_str());
    }

    if (g_long_term_memory) g_long_term_memory->storeMessage("user", input_str);

    // 1. Trigger Core Heartbeat (v3.9 logic)
    Input minimalist_input = {};
    size_t len = std::min(input_str.length(), sizeof(minimalist_input.data) - 1);
    memcpy(minimalist_input.data, input_str.c_str(), len);
    minimalist_input.length = len;

    if (g_ronin_kernel) {
        g_ronin_kernel->tick(minimalist_input);
    }

    // 2. Routing Decision
    CognitiveIntent intent = defaultIntentProcessor(minimalist_input);
    Node* next_node = g_graph_executor->selectNextNode(input_str);
    std::string response = "Input processed via Reasoning Spine (No specific capability triggered).";

    if (next_node) {
        if (g_memory_manager) g_memory_manager->clearContext();

        if ((next_node->id == 2 || next_node->id == 3) && g_file_search_node) {
            LOGI(TAG, "Routing to Hybrid FileSearch capability.");
            auto results = g_file_search_node->execute(input_str);
            if (!results.empty()) {
                response = results[0];
            }
        } else if (next_node->id == 4) {
            response = std::string("System: Flashlight ") + (intent.intent_param ? "ON" : "OFF") + "... [v3.9.1-STABLE]";
        } else if (next_node->id == 5) {
            response = "System: Locating device... GPS Link Established.";
        } else if (next_node->id == 6) {
            response = std::string("System: WiFi ") + (intent.intent_param ? "ENABLED" : "DISABLED") + ".";
        } else if (next_node->id == 7) {
            response = std::string("System: Bluetooth ") + (intent.intent_param ? "ENABLED" : "DISABLED") + ".";
        }
    }

    if (g_long_term_memory) g_long_term_memory->storeMessage("ronin", response);

    return env->NewStringUTF(response.c_str());
}

JNIEXPORT jobjectArray JNICALL
Java_com_ronin_kernel_NativeEngine_getChatHistory(JNIEnv *env, jobject thiz) {
    if (!g_long_term_memory) return nullptr;
    auto history = g_long_term_memory->getHistory(50);
    
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(history.size() * 2, stringClass, nullptr);
    
    for (size_t i = 0; i < history.size(); ++i) {
        env->SetObjectArrayElement(result, i * 2, env->NewStringUTF(history[i].first.c_str()));
        env->SetObjectArrayElement(result, i * 2 + 1, env->NewStringUTF(history[i].second.c_str()));
    }
    
    return result;
}

JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_getLMKPressure(JNIEnv *env, jobject thiz) {
    return g_memory_manager ? static_cast<jint>(g_memory_manager->getPressureScore()) : 0;
}

JNIEXPORT jint JNICALL
Java_com_ronin_kernel_NativeEngine_runMaintenance(JNIEnv *env, jobject thiz, jboolean is_charging) {
    return g_long_term_memory ? static_cast<jint>(g_long_term_memory->runMaintenance(is_charging == JNI_TRUE)) : 0;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setEngineInstance(JNIEnv *env, jobject thiz) {
    env->GetJavaVM(&g_vm);
    if (g_engine_instance) env->DeleteGlobalRef(g_engine_instance);
    g_engine_instance = env->NewGlobalRef(thiz);
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateSystemHealth(JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total) {
    // Return true if RAM > 85%
    if (total > 0 && (used / total) > 0.85f) {
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

} // extern "C"
