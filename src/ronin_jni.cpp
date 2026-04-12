#include "ronin_jni.h"
#include "intent_engine.h"
#include "memory_manager.h"
#include "long_term_memory.h"
#include "checkpoint_engine.h"
#include "capability_graph.h"
#include "graph_storage.h"
#include "graph_executor.h"
#include "ronin_log.h"
#include "checkpoint_schema_generated.h"
#include <cstdint>
#include <memory>
#include <string>

#include "file_search_engine.h"
#include <string>

#define TAG "RoninNativeEngine"

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
static std::unique_ptr<FileSearchEngine> g_file_search_engine;

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
    g_file_search_engine.reset();

    // 1. Initialize Memory Components
    g_long_term_memory = std::make_unique<LongTermMemory>(base_path + "/ronin_l3.db");
    g_memory_manager = std::make_unique<MemoryManager>(20);
    g_memory_manager->setLongTermMemory(g_long_term_memory.get());

    // 2. Initialize Checkpoint and File Search
    g_checkpoint_engine = std::make_unique<CheckpointEngine>(base_path + "/checkpoint.bin");
    g_checkpoint_engine->initializeShadowBuffer(1024 * 1024);
    g_file_search_node = std::make_unique<FileSearchNode>(*g_long_term_memory);

    // 3. Initialize Reasoning Spine (Graph)
    g_graph_storage = std::make_unique<GraphStorage>(base_path + "/ronin_graph.db");
    g_capability_graph = std::make_unique<CapabilityGraph>();
    g_graph_storage->loadGraph(*g_capability_graph);
    
    // Default nodes for prototype
    g_capability_graph->addNode(1, "Reasoning_Engine");
    g_capability_graph->addNode(2, "File_Search");
    g_capability_graph->addEdge(1, 2, 1.0f);

    g_graph_executor = std::make_unique<GraphExecutor>(*g_capability_graph, *g_graph_storage);

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

JNIEXPORT jfloat JNICALL
Java_com_ronin_kernel_NativeEngine_processInput(JNIEnv *env, jobject thiz, jobject input) {
    if (input == nullptr || !g_memory_manager || !g_graph_executor) return 0.0f;
    void* ptr = env->GetDirectBufferAddress(input);
    if (ptr == nullptr) return 0.0f;

    // 1. Simulate Intent Detection (Mock: if input contains 'search')
    // In a real build, this would use compute_intent_similarity_neon
    std::string input_str = "search"; // Mocked for demonstration

    // 2. Routing Decision
    float divergence = 0.5f; 
    uint32_t next_node = g_graph_executor->selectNextNode(1, divergence);

    if (next_node == 2) { // File_Search node
        LOGI(TAG, "Routing to File_Search capability.");
        auto results = g_file_search_engine->searchFiles("demo_query");
        LOGI(TAG, "File Search returned %zu results.", results.size());
    }

    Token t = {1, 0.9f, {0.1f, 0.2f}}; 
    g_memory_manager->addRecentToken(t);
    return 1.0f;
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
ce(is_charging == JNI_TRUE)) : 0;
}

} // extern "C"
