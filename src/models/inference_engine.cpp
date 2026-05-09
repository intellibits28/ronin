#include "models/inference_engine.h"
#include "models/hydration_manager.h"
#include "hal/shared_memory_bridge.h"
#include "models/prompt_factory.h"
#include "ronin_log.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <dlfcn.h>

#define TAG "RoninInferenceEngine"

namespace Ronin::Kernel::Model {

// --- Stable Flat C API Definition (Phase 5.5) ---
typedef void* LlmInferenceEngineHandle;
typedef void* LlmInferenceSessionHandle;

struct LlmModelSettings {
    const char* model_path;
    const char* cache_dir;
    int max_num_tokens;
    int preferred_backend; // 0=GPU, 1=CPU
};

typedef void (*LlmProgressCallback)(void* user_data, const char** partial_results, int count, bool done);

// Function pointer types
typedef int (*LlmCreateEngineFunc)(const LlmModelSettings*, LlmInferenceEngineHandle*, char**);
typedef int (*LlmCreateSessionFunc)(LlmInferenceEngineHandle, void*, LlmInferenceSessionHandle*, char**);
typedef int (*LlmPredictAsyncFunc)(LlmInferenceSessionHandle, const char*, LlmProgressCallback, void*, char**);
typedef void (*LlmDestroyEngineFunc)(LlmInferenceEngineHandle);
typedef void (*LlmDestroySessionFunc)(LlmInferenceSessionHandle);

struct InferenceEngine::Impl {
    std::string model_path;
    std::string base_path;
    std::string lib_path;
    int context_window = 2048;
    
    HydrationManager hydration_manager;
    std::unique_ptr<HAL::SharedMemoryBridge<HAL::SpineRingBuffer>> spine_bridge;
    
    // Opaque Handles
    LlmInferenceEngineHandle engine_handle = nullptr;
    LlmInferenceSessionHandle session_handle = nullptr;
    void* lib_handle = nullptr;

    // Resolved Function Pointers
    LlmCreateEngineFunc f_create_engine = nullptr;
    LlmCreateSessionFunc f_create_session = nullptr;
    LlmPredictAsyncFunc f_predict_async = nullptr;
    LlmDestroyEngineFunc f_destroy_engine = nullptr;
    LlmDestroySessionFunc f_destroy_session = nullptr;

    std::atomic<uint32_t> sequence_counter{0};
    std::mutex inference_mutex;
    bool is_processing = false;

    Impl(const std::string& path) : model_path(path) {}

    ~Impl() {
        if (f_destroy_session && session_handle) f_destroy_session(session_handle);
        if (f_destroy_engine && engine_handle) f_destroy_engine(engine_handle);
        if (lib_handle) dlclose(lib_handle);
    }

    bool initLlm() {
        if (!hydration_manager.hydrate(model_path)) {
            LOGE(TAG, "Failed to hydrate model: %s", model_path.c_str());
            return false;
        }

        // Phase 5.5: Dynamic Loading with Explicit Path
        std::string lib_full_path = "libllm_inference_engine_jni.so";
        if (!lib_path.empty()) {
            lib_full_path = lib_path + "/libllm_inference_engine_jni.so";
            LOGI(TAG, "Attempting dlopen with explicit path: %s", lib_full_path.c_str());
        }

        lib_handle = dlopen(lib_full_path.c_str(), RTLD_NOW);
        if (!lib_handle) {
            const char* err = dlerror();
            LOGE(TAG, "dlopen failed for %s: %s", lib_full_path.c_str(), err ? err : "Unknown Error");
            // Fallback to library name only
            lib_handle = dlopen("libllm_inference_engine_jni.so", RTLD_NOW);
            if (!lib_handle) return false;
        }

        // Probing Loop for Stable C Symbols
        auto probe = [&](const char* base_name) -> void* {
            void* sym = dlsym(lib_handle, base_name);
            if (sym) return sym;
            
            // Try with leading underscore (common in some NDK/toolchains)
            std::string underscore = "_" + std::string(base_name);
            sym = dlsym(lib_handle, underscore.c_str());
            if (sym) return sym;

            LOGW(TAG, "dlsym failed for %s: %s", base_name, dlerror());
            return nullptr;
        };

        f_create_engine = (LlmCreateEngineFunc)probe("LlmInferenceEngine_CreateEngine");
        f_create_session = (LlmCreateSessionFunc)probe("LlmInferenceEngine_CreateSession");
        f_predict_async = (LlmPredictAsyncFunc)probe("LlmInferenceEngine_PredictAsync");
        f_destroy_engine = (LlmDestroyEngineFunc)probe("LlmInferenceEngine_DestroyEngine");
        f_destroy_session = (LlmDestroySessionFunc)probe("LlmInferenceEngine_DestroySession");

        if (!f_create_engine || !f_create_session || !f_predict_async) {
            LOGE(TAG, "FATAL: Could not resolve stable C symbols from libllm_inference_engine_jni.so");
            return false;
        }

        LlmModelSettings settings = {
            .model_path = model_path.c_str(),
            .cache_dir = base_path.c_str(),
            .max_num_tokens = context_window,
            .preferred_backend = 0 // GPU
        };

        char* err_msg = nullptr;
        if (f_create_engine(&settings, &engine_handle, &err_msg) != 0) {
            LOGE(TAG, "LlmInferenceEngine_CreateEngine failed: %s", err_msg ? err_msg : "Unknown Error");
            if (err_msg) free(err_msg);
            return false;
        }

        if (f_create_session(engine_handle, nullptr, &session_handle, &err_msg) != 0) {
            LOGE(TAG, "LlmInferenceEngine_CreateSession failed: %s", err_msg ? err_msg : "Unknown Error");
            if (err_msg) free(err_msg);
            return false;
        }

        // Initialize SpineBridge with dynamic base_path
        spine_bridge = std::make_unique<HAL::SharedMemoryBridge<HAL::SpineRingBuffer>>("spine_stream");
        if (!spine_bridge->create(base_path, true)) {
            LOGE(TAG, "Failed to create SHM Spine Bridge at %s", base_path.c_str());
            return false;
        }

        LOGI(TAG, "Stable Native Inference Engine Active.");
        return true;
    }
};

InferenceEngine::InferenceEngine(const std::string& modelPath) 
    : m_impl(std::make_unique<Impl>(modelPath)) {}

InferenceEngine::~InferenceEngine() = default;

void InferenceEngine::setBasePath(const std::string& path) {
    if (m_impl) m_impl->base_path = path;
}

void InferenceEngine::setLibPath(const std::string& path) {
    if (m_impl) m_impl->lib_path = path;
}

bool InferenceEngine::loadModel(const std::string& path) {
    m_impl->model_path = path;
    return m_impl->initLlm();
}

bool InferenceEngine::isLoaded() const {
    return m_impl->engine_handle != nullptr;
}

std::string InferenceEngine::runLiteRTReasoning(const std::string& input) {
    if (!isLoaded()) return "Error: Inference Engine not loaded.";

    {
        std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
        if (m_impl->is_processing) return "Error: Engine Busy.";
        m_impl->is_processing = true;
    }

    uint32_t seq_id = ++m_impl->sequence_counter;
    
    // Background Thread for Non-blocking Reasoning
    std::thread([this, input, seq_id]() {
        try {
            LOGI(TAG, "Starting Stable C-API Reasoning [Seq: %u]", seq_id);
            
            PromptFactory::BackendType type = PromptFactory::BackendType::LOCAL_GEMMA_2;
            if (m_impl->model_path.find("gemma-4") != std::string::npos || 
                m_impl->model_path.find(".litertlm") != std::string::npos) {
                type = PromptFactory::BackendType::LOCAL_GEMMA_4;
            }
            std::string wrapped_input = PromptFactory::wrap(input, type);

            // C Callback Bridge
            auto callback = [](void* user_data, const char** partial_results, int count, bool done) {
                auto* self = static_cast<InferenceEngine*>(user_data);
                uint32_t current_seq = self->m_impl->sequence_counter.load();
                
                if (self->m_impl->spine_bridge) {
                    HAL::SpineRingBuffer* rb = self->m_impl->spine_bridge->get();
                    if (rb) {
                        HAL::InferencePacket packet;
                        packet.sequence_id = current_seq;
                        packet.token_id = 0; 
                        packet.confidence = 1.0f;
                        packet.is_final = done;
                        
                        std::string combined;
                        for (int i = 0; i < count; ++i) combined += partial_results[i];
                        
                        std::strncpy(packet.fragment, combined.c_str(), sizeof(packet.fragment) - 1);
                        packet.fragment[sizeof(packet.fragment) - 1] = '\0';
                        
                        int retries = 10;
                        while (!rb->push(packet) && retries-- > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                }
            };

            char* err_msg = nullptr;
            if (m_impl->f_predict_async(m_impl->session_handle, wrapped_input.c_str(), callback, this, &err_msg) != 0) {
                LOGE(TAG, "PredictAsync Error: %s", err_msg ? err_msg : "Unknown Error");
                if (err_msg) free(err_msg);
            }

        } catch (const std::exception& e) {
            LOGE(TAG, "Inference Thread Exception: %s", e.what());
        } catch (...) {
            LOGE(TAG, "Unknown Inference Thread Exception");
        }

        {
            std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
            m_impl->is_processing = false;
        }
    }).detach();

    return "Reasoning Started [SHM Active]";
}

std::string InferenceEngine::escalateToCloud(const std::string& input, const std::string& apiKey, const std::string& provider) {
    return ""; // Optional: Cloud fallback handled in JNI
}

int InferenceEngine::classifyCoarse(const std::string& input) { return 1; }
CognitiveIntent InferenceEngine::predictFine(const std::string& input, int coarse_category) { return {1, 1.0f, true}; }
std::string InferenceEngine::getModelPath() const { return m_impl->model_path; }
std::string InferenceEngine::getRuntimeInfo() const { return "Runtime: Native Flat-C API"; }
long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { m_impl->context_window = tokens; }
void InferenceEngine::purgeKVCache() { LOGI(TAG, "Purging Native KV Cache..."); }

} // namespace Ronin::Kernel::Model
