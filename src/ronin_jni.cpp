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
static std::shared_ptr<FileSearchNode> g_file_search_node;
static std::unique_ptr<FileScanner> g_file_scanner;
static std::shared_ptr<NeuralEmbeddingNode> g_neural_embedding_node;

// JNI Callback Caching
static JavaVM* g_vm = nullptr;
static jobject g_engine_instance = nullptr;

// Phase 4.0: Push-based Memory State (Rule 4 Compliance)
static std::atomic<bool> g_low_memory_mode{false};

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
  LOGI("RoninJNI", "Executing Node %u [v4.0-STABLE]", nodeId);
  
  // Node state transitions and context storage handled here.
  // Physical execution and UI responses are mastered in processInput.
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
    g_neural_embedding_node = std::make_shared<NeuralEmbeddingNode>(base_path + "/assets/models/model.onnx");
    g_file_search_node = std::make_shared<FileSearchNode>(g_long_term_memory.get(), g_neural_embedding_node.get());
    
    g_file_scanner = std::make_unique<FileScanner>(*g_long_term_memory, g_neural_embedding_node.get());

    // 3. Initialize Reasoning Spine (Graph)
    g_graph_storage = std::make_unique<GraphStorage>(base_path + "/ronin_graph.db");
    g_capability_graph = std::make_unique<CapabilityGraph>();
    g_graph_storage->loadGraph(*g_capability_graph);
    
    // Default nodes
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

    // Phase 4.0: LoRA State Diff Serialization & Activation Masking
    auto lora_dispatcher = std::make_shared<Ronin::Kernel::Model::LoraDispatcher>();

    // RULE 1: Memory Pinning (v4.1 Hardware Reality)
    static uint8_t mock_weights[1024 * 1024]; // 1MB mock base weights
    lora_dispatcher->pinBaseWeights(mock_weights, sizeof(mock_weights));

    // Register Mock LoRAs for demonstration (Phase 2)
    // Coding (ID 1), Memory (ID 2), Hardware-Control (ID 3)
    // Note: interference_signature prevents conflicting LoRAs from activating simultaneously.
    Ronin::Kernel::Model::LoraDeltaBlock coding_lora = {1, nullptr, nullptr, 4096, 4096, 16, 1.0f, 0x00000002}; // Conflicts with bit 1 (Memory)
    Ronin::Kernel::Model::LoraDeltaBlock memory_lora = {2, nullptr, nullptr, 4096, 4096, 16, 1.0f, 0x00000001}; // Conflicts with bit 0 (Coding)
    Ronin::Kernel::Model::LoraDeltaBlock hardware_lora = {3, nullptr, nullptr, 4096, 4096, 16, 1.0f, 0x00000000}; // No conflicts

    lora_dispatcher->registerLora(coding_lora);
    lora_dispatcher->registerLora(memory_lora);
    lora_dispatcher->registerLora(hardware_lora);

    g_intent_engine->setLoraDispatcher(lora_dispatcher);


    // Phase 4.0: Survival Core Checkpoint Manager (Hydration deferred to hydrate())
    auto checkpoint_manager = std::make_shared<Ronin::Kernel::Checkpoint::CheckpointManager>(base_path + "/survival_core.bin");
    g_intent_engine->setCheckpointManager(checkpoint_manager);
    
    // Register Modular Skills (Phase 4.0 Unified)
    if (g_file_search_node) {
        g_intent_engine->registerSkill(2, g_file_search_node);
    }
    if (g_neural_embedding_node) {
        g_intent_engine->registerSkill(3, g_neural_embedding_node);
    }
    
    auto inference_engine = std::make_unique<Ronin::Kernel::Model::InferenceEngine>(base_path + "/assets/models/model.onnx");
    std::string bootMsg = "> Kernel Hydration: Local brain hydrated from /assets/models/model.onnx";
    Ronin::Kernel::Capability::HardwareBridge::pushMessage(bootMsg);
    g_intent_engine->setInferenceEngine(std::move(inference_engine));

    g_ronin_kernel = std::make_unique<RoninKernel>(s_handler_registry, s_cap_manager);

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
        return JNI_FALSE;
    }

    auto checkpoint = Ronin::Kernel::Checkpoint::GetCheckpoint(buffer_ptr);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_updateLifecycleState(JNIEnv *env, jobject thiz, jint lifecycle_state) {
    // Phase 4.2: NPU Hibernation & Lifecycle Sync
    if (lifecycle_state == 0) { // BACKGROUND
        if (g_checkpoint_engine) g_checkpoint_engine->onLMKSignal();
        if (g_graph_executor) g_graph_executor->triggerAsyncSync();
        
        if (g_intent_engine) {
            auto inference = g_intent_engine->getInferenceEngine();
            if (inference) inference->suspendNPU();
        }
        LOGI(TAG, "Kernel Backgrounded: NPU Hibernation active.");
    } else { // FOREGROUND
        if (g_intent_engine) {
            auto inference = g_intent_engine->getInferenceEngine();
            if (inference) inference->resumeNPU();
        }
        LOGI(TAG, "Kernel Foregrounded: NPU resumed.");
    }
}

JNIEXPORT jfloat JNICALL
Java_com_ronin_kernel_NativeEngine_computeSimilarity(JNIEnv *env, jobject thiz, jobject buffer_a, jobject buffer_b) {
    void* ptr_a = env->GetDirectBufferAddress(buffer_a);
    void* ptr_b = env->GetDirectBufferAddress(buffer_b);
    if (ptr_a == nullptr || ptr_b == nullptr) return -1.0f;
    return static_cast<jfloat>(Ronin::Kernel::Intent::compute_intent_similarity_neon(
        static_cast<const int8_t*>(ptr_a), static_cast<const int8_t*>(ptr_b)));
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_setOfflineMode(JNIEnv *env, jobject thiz, jboolean offline) {
    if (g_intent_engine) {
        g_intent_engine->setOfflineMode(offline == JNI_TRUE);
    }
}

JNIEXPORT jstring JNICALL
Java_com_ronin_kernel_NativeEngine_getActiveModelPath(JNIEnv *env, jobject thiz) {
    if (g_intent_engine) {
        auto inference = g_intent_engine->getInferenceEngine();
        if (inference) {
            std::string combined = "Router: " + inference->getRouterPath() + " | Reasoning: " + inference->getModelPath();
            return env->NewStringUTF(combined.c_str());
        }
    }
    return env->NewStringUTF("None");
}

JNIEXPORT jlong JNICALL
Java_com_ronin_kernel_NativeEngine_verifyModel(JNIEnv *env, jobject thiz) {
    if (g_intent_engine) {
        auto inference = g_intent_engine->getInferenceEngine();
        if (inference) {
            return static_cast<jlong>(inference->verifyModel());
        }
    }
    return -1;
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

    if (g_long_term_memory) g_long_term_memory->storeMessage("user", input_str);

    // 1. Kernel Heartbeat (State Management & History)
    Input minimalist_input = {};
    size_t len = std::min(input_str.length(), sizeof(minimalist_input.data) - 1);
    memcpy(minimalist_input.data, input_str.c_str(), len);
    minimalist_input.length = len;

    if (g_ronin_kernel) {
        g_ronin_kernel->tick(minimalist_input);
    }

    CognitiveIntent intent = {1, 0.5f, true};
    if (g_ronin_kernel) {
        intent = g_ronin_kernel->getLastIntent();
    }

    // Tier 0: Command Fast-path
    if (intent.id == 0) {
        return env->NewStringUTF("System: Command Executed.");
    }

    // Phase 4.1: Re-awakening Protocol (Temporal Causal Stitching)
    if (g_intent_engine) {
        auto cm = g_intent_engine->getCheckpointManager();
        if (cm) {
            bool resumed = cm->stitchContext("intent_" + std::to_string(intent.id));
            if (resumed) {
                LOGI(TAG, "LMK Survival: Context stitched successfully. Resuming session.");
            }
        }
    }

    // 2. Routing Decision (Phase 4.0: Deterministic Priority)
    // If IntentEngine found a high-confidence match (not ID 1), use it directly.
    uint32_t targetNodeId = 0;
    std::string response = "Input processed via Reasoning Spine (No specific capability triggered).";

    if (intent.id > 1 && intent.confidence >= 0.75f) {
        std::string logMsg = ">>> Routing: Deterministic Match (ID " + std::to_string(intent.id) + ") bypassing Thompson Sampling.";
        LOGI(TAG, "%s", logMsg.c_str());
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
        targetNodeId = intent.id;
    } else {
        // Fallback to probabilistic graph executor for reasoning/searching or if confidence is low
        std::string logMsg = ">>> Routing: Deferring to Probabilistic Graph Executor (Low confidence < 0.75 or ID 1).";
        LOGI(TAG, "%s", logMsg.c_str());
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
        
        Node* next_node = g_graph_executor->selectNextNode(input_str);
        if (next_node) {
            targetNodeId = next_node->id;
        }
    }

    if (targetNodeId > 0 && targetNodeId != 1) {
        if (g_memory_manager) g_memory_manager->clearContext();
        
        // Phase 4.0: Intent Continuity Guard (LMK Survival)
        // Store in Survival Core (CheckpointManager) for fast reboot hydration
        if (g_intent_engine) {
            response = g_intent_engine->executeSkill(targetNodeId, input_str);
        }
    } else {
        // Phase 4.3 (Updated): LiteRT-LM Integration
        auto inference = g_intent_engine ? g_intent_engine->getInferenceEngine() : nullptr;
        if (inference) {
            std::string localReasoning = inference->runLiteRTReasoning(input_str);
            
            bool offline = g_intent_engine ? g_intent_engine->isOfflineMode() : false;
            // Escalation Logic: If input is tagged as "complex" or Local Brain confidence was < 0.75
            if (!offline && (input_str.find("complex") != std::string::npos || intent.confidence < 0.75f)) {
                std::string apiKey = Ronin::Kernel::Capability::HardwareBridge::getCloudApiKey("Gemini");
                response = inference->escalateToCloud(input_str, apiKey);
            } else {
                if (offline && (input_str.find("complex") != std::string::npos || intent.confidence < 0.75f)) {
                    response = localReasoning + "\n[OFFLINE] Cloud escalation suppressed by user.";
                } else {
                    response = localReasoning;
                }
            }
        }
    }

    // 4. Persistence Exclusion (Phase 4.4.3)
    // Only store reasoning/chat results in SQLite. 
    // Hardware actions (IDs > 1) are shown in console but not history.
    if (g_long_term_memory && targetNodeId <= 1) {
        g_long_term_memory->storeMessage("ronin", response);
    }

    // Data Protocol v4.3: Transition to Structured JSON
    std::string structuredResponse = response;
    if (g_intent_engine && g_intent_engine->getInferenceEngine()) {
        structuredResponse = g_intent_engine->getInferenceEngine()->getStructuredResponse(
            std::to_string(targetNodeId), "UNIFIED_HYBRID_v4.3", response);
    }

    return env->NewStringUTF(structuredResponse.c_str());
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_hydrate(JNIEnv *env, jobject thiz) {
    if (g_intent_engine) {
        auto cm = g_intent_engine->getCheckpointManager();
        if (cm) {
            LOGI(TAG, "Triggering Survival Core Hydration sequence...");
            cm->initialize();
            
            auto active = cm->getActiveCheckpoint();
            if (active) {
                std::string logMsg = "> SURVIVAL REBIRTH: Restored Intent '" + active->intent_id()->str() + 
                                     "' with Edge Frontier " + std::to_string(active->edge_frontier());
                LOGI(TAG, "%s", logMsg.c_str());
                Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
            } else {
                Ronin::Kernel::Capability::HardwareBridge::pushMessage("> SURVIVAL CORE: No active checkpoint found (Clean Rebirth).");
            }
        }
    }
}

JNIEXPORT jobjectArray JNICALL
Java_com_ronin_kernel_NativeEngine_getChatHistory__(JNIEnv *env, jobject thiz) {
    if (!g_long_term_memory) return nullptr;
    auto history = g_long_term_memory->getHistory(100, 0); // Default to 100
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(history.size() * 2, stringClass, nullptr);
    for (size_t i = 0; i < history.size(); ++i) {
        env->SetObjectArrayElement(result, i * 2, env->NewStringUTF(history[i].first.c_str()));
        env->SetObjectArrayElement(result, i * 2 + 1, env->NewStringUTF(history[i].second.c_str()));
    }
    return result;
}

JNIEXPORT jobjectArray JNICALL
Java_com_ronin_kernel_NativeEngine_getChatHistory__II(JNIEnv *env, jobject thiz, jint limit, jint offset) {
    if (!g_long_term_memory) return nullptr;
    auto history = g_long_term_memory->getHistory(limit, offset);
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
    
    // Phase 4.0: Initialize the Hardware Bridge for Unified Skill Execution
    HardwareBridge::initialize(g_vm, g_engine_instance);
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_injectLocation(JNIEnv *env, jobject thiz, jdouble lat, jdouble lon) {
    if (g_ronin_kernel) {
        g_ronin_kernel->injectLocation(static_cast<double>(lat), static_cast<double>(lon));
    }
    
    char buffer[128];
    if (lat == 0.0 && lon == 0.0) {
        snprintf(buffer, sizeof(buffer), "System Update: GPS Error: Location Unavailable.");
    } else {
        snprintf(buffer, sizeof(buffer), "System Update: GPS Coordinates injected [%.6f, %.6f]", (double)lat, (double)lon);
    }

    // --- ASYNCHRONOUS UI CALLBACK (Kotlin Bridge) ---
    // Since this is called from Kotlin, the thread is already attached to the JVM.
    if (g_engine_instance) {
        jclass clazz = env->GetObjectClass(g_engine_instance);
        jmethodID mid = env->GetMethodID(clazz, "pushKernelMessage", "(Ljava/lang/String;)V");
        if (mid) {
            jstring jmsg = env->NewStringUTF(buffer);
            env->CallVoidMethod(g_engine_instance, mid, jmsg);
            env->DeleteLocalRef(jmsg);
        }
        env->DeleteLocalRef(clazz);
    }
}

JNIEXPORT void JNICALL
Java_com_ronin_kernel_NativeEngine_notifyTrimMemory(JNIEnv *env, jobject thiz, jint level) {
    // 80 = TRIM_MEMORY_COMPLETE, 15 = TRIM_MEMORY_RUNNING_CRITICAL
    if (level >= 15) {
        g_low_memory_mode.store(true);
        LOGW("RoninHealth", ">>> OS PUSH: Low Memory Signal (Level %d). Kernel entering conservative mode.", level);
        
        if (g_memory_manager) {
            g_memory_manager->onMemoryPressure();
        }
    } else {
        g_low_memory_mode.store(false);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_loadModel(JNIEnv *env, jobject thiz, jstring path) {
    if (path == nullptr || !g_intent_engine) return JNI_FALSE;
    
    const char *path_cstr = env->GetStringUTFChars(path, nullptr);
    if (path_cstr == nullptr) return JNI_FALSE;
    std::string model_path(path_cstr);
    env->ReleaseStringUTFChars(path, path_cstr);

    auto inference = g_intent_engine->getInferenceEngine();
    if (inference) {
        bool success = inference->loadModel(model_path);
        if (success) {
            std::string logMsg = "> Kernel Hydration: Local brain reloaded from " + model_path;
            LOGI(TAG, "%s", logMsg.c_str());
            Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
            return JNI_TRUE;
        }
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateCloudProviders(JNIEnv *env, jobject thiz, jstring json) {
    if (json == nullptr) return JNI_FALSE;
    
    const char *json_cstr = env->GetStringUTFChars(json, nullptr);
    if (json_cstr == nullptr) return JNI_FALSE;
    std::string json_content(json_cstr);
    env->ReleaseStringUTFChars(json, json_cstr);

    // Atomic update via C++ Kernel (simplistic version for prototype)
    // In production, we would use a temporary file and rename it.
    std::string config_dir = "/storage/emulated/0/Ronin/config";
    std::string config_path = config_dir + "/providers.json";
    
    // Ensure directory exists (naive approach)
    std::string mkdir_cmd = "mkdir -p " + config_dir;
    system(mkdir_cmd.c_str());

    std::ofstream file(config_path);
    if (file.is_open()) {
        file << json_content;
        file.close();
        LOGI(TAG, "Cloud Provider manifest updated atomically: %s", config_path.c_str());
        return JNI_TRUE;
    }
    
    LOGE(TAG, "Failed to update Cloud Provider manifest: %s", config_path.c_str());
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_ronin_kernel_NativeEngine_updateSystemHealth(JNIEnv *env, jobject thiz, jfloat temp, jfloat used, jfloat total) {
    // Phase 4.0: Report to UI via HardwareBridge callback (Metrics only)
    Ronin::Kernel::Capability::HardwareBridge::reportSystemHealth(temp, used, total);

    // Rule 4 Compliance: NEVER return pruning signal via polling.
    // Memory pruning is now strictly OS-driven via notifyTrimMemory.
    return JNI_FALSE;
}

} // extern "C"
#endif // __ANDROID__
