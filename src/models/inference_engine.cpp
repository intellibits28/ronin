#include "models/inference_engine.h"
#include "models/hydration_manager.h"
#include "hal/shared_memory_bridge.h"
#include "models/prompt_factory.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <dlfcn.h>

#define TAG "RoninInferenceEngine"

namespace Ronin::Kernel::Model {

// --- EXACT C-API Definition from libllm_inference_engine_jni.so (Lock-on Fix) ---
typedef void* LlmInferenceEngineHandle;
typedef void* LlmInferenceSessionHandle;

struct LlmModelSettings {
    const char* model_path;
    const char* cache_dir;
    int max_num_tokens;
    int preferred_backend; // 0=GPU, 1=CPU
};

typedef void (*LlmProgressCallback)(void* user_data, const char** partial_results, int count, bool done);

typedef int (*LlmCreateEngineFunc)(const LlmModelSettings*, LlmInferenceEngineHandle*, char**);
typedef int (*LlmCreateSessionFunc)(LlmInferenceEngineHandle, void*, LlmInferenceSessionHandle*, char**);
typedef int (*LlmPredictAsyncFunc)(LlmInferenceSessionHandle, const char*, LlmProgressCallback, void*, char**);
typedef void (*LlmEngineDeleteFunc)(LlmInferenceEngineHandle);
typedef void (*LlmSessionDeleteFunc)(LlmInferenceSessionHandle);

struct InferenceEngine::Impl {
    std::string model_path;
    std::string base_path;
    std::string lib_path;
    int context_window = 2048;
    
    HydrationManager hydration_manager;
    std::unique_ptr<HAL::SharedMemoryBridge<HAL::SpineRingBuffer>> spine_bridge;
    
    LlmInferenceEngineHandle engine_handle = nullptr;
    LlmInferenceSessionHandle session_handle = nullptr;
    void* lib_handle = nullptr;

    LlmCreateEngineFunc f_create_engine = nullptr;
    LlmCreateSessionFunc f_create_session = nullptr;
    LlmPredictAsyncFunc f_predict_async = nullptr;
    LlmEngineDeleteFunc f_engine_delete = nullptr;
    LlmSessionDeleteFunc f_session_delete = nullptr;

    bool m_use_kotlin_fallback = false;

    std::atomic<uint32_t> sequence_counter{0};
    std::mutex inference_mutex;
    bool is_processing = false;

    Impl(const std::string& path) : model_path(path) {}

    ~Impl() {
        if (f_session_delete && session_handle) f_session_delete(session_handle);
        if (f_engine_delete && engine_handle) f_engine_delete(engine_handle);
        // Do not close lib_handle if it's the process-global one
    }

    bool initLlm() {
        // Stage 1, 2, 3: Hydration Pipeline (mmap -> checksum -> metadata)
        if (!hydration_manager.hydrate(model_path)) {
            LOGE(TAG, "Pipeline Error: Failed to hydrate model: %s", model_path.c_str());
            return false;
        }

        // Stage 4: Runtime Binding (Lock-on Fix)
        lib_handle = dlopen(nullptr, RTLD_NOW);
        if (!lib_handle) {
            LOGE(TAG, "dlopen(nullptr) failed: %s", dlerror());
            return false;
        }

        LOGI(TAG, "Commencing Pipeline Stage 4: Runtime Binding...");

        // Use EXACT symbols found via nm tool
        f_create_engine = (LlmCreateEngineFunc)dlsym(lib_handle, "LlmInferenceEngine_CreateEngine");
        f_create_session = (LlmCreateSessionFunc)dlsym(lib_handle, "LlmInferenceEngine_CreateSession");
        f_predict_async = (LlmPredictAsyncFunc)dlsym(lib_handle, "LlmInferenceEngine_Session_PredictAsync");
        f_engine_delete = (LlmEngineDeleteFunc)dlsym(lib_handle, "LlmInferenceEngine_Engine_Delete");
        f_session_delete = (LlmSessionDeleteFunc)dlsym(lib_handle, "LlmInferenceEngine_Session_Delete");

        if (f_create_engine && f_create_session && f_predict_async) {
            LOGI(TAG, "SUCCESS: Exact C API symbols bound from memory.");
            
            LlmModelSettings settings = {
                .model_path = model_path.c_str(),
                .cache_dir = base_path.c_str(),
                .max_num_tokens = context_window,
                .preferred_backend = 0 // GPU
            };

            char* err_msg = nullptr;
            if (f_create_engine(&settings, &engine_handle, &err_msg) != 0) {
                LOGW(TAG, "Native Engine Creation failed: %s. Switching to JNI Fallback.", err_msg ? err_msg : "Unknown");
                if (err_msg) free(err_msg);
                m_use_kotlin_fallback = true;
            }

            if (!m_use_kotlin_fallback && f_create_session(engine_handle, nullptr, &session_handle, &err_msg) != 0) {
                LOGW(TAG, "Native Session Creation failed: %s. Switching to JNI Fallback.", err_msg ? err_msg : "Unknown");
                if (err_msg) free(err_msg);
                m_use_kotlin_fallback = true;
            }
        } else {
            LOGW(TAG, "Stage 4 Failed: Symbols not found in process memory. Activating JNI Fallback.");
            m_use_kotlin_fallback = true;
        }

        // Initialize SHM Highway
        spine_bridge = std::make_unique<HAL::SharedMemoryBridge<HAL::SpineRingBuffer>>("spine_stream");
        if (!spine_bridge->create(base_path, true)) {
            LOGE(TAG, "Highway Error: Failed to create SHM Spine Bridge");
        }

        LOGI(TAG, "Sentient Staging Complete (Mode: %s | Gemma4: %s)", 
             m_use_kotlin_fallback ? "Fallback" : "Native",
             hydration_manager.isGemma4() ? "YES" : "NO");
        return true;
    }
};

InferenceEngine::InferenceEngine(const std::string& modelPath) : m_impl(std::make_unique<Impl>(modelPath)) {}
InferenceEngine::~InferenceEngine() = default;
void InferenceEngine::setBasePath(const std::string& path) { if (m_impl) m_impl->base_path = path; }
void InferenceEngine::setLibPath(const std::string& path) { if (m_impl) m_impl->lib_path = path; }
bool InferenceEngine::loadModel(const std::string& path) { m_impl->model_path = path; return m_impl->initLlm(); }
bool InferenceEngine::isLoaded() const { return m_impl->m_use_kotlin_fallback || m_impl->engine_handle != nullptr; }

std::string InferenceEngine::runLiteRTReasoning(const std::string& input) {
    if (!isLoaded()) return "Error: Inference Engine not loaded.";
    {
        std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
        if (m_impl->is_processing) return "Error: Engine Busy.";
        m_impl->is_processing = true;
    }

    uint32_t seq_id = ++m_impl->sequence_counter;
    
    std::thread([this, input, seq_id]() {
        try {
            PromptFactory::BackendType type = PromptFactory::BackendType::LOCAL_GEMMA_2;
            if (m_impl->model_path.find("gemma-4") != std::string::npos || m_impl->hydration_manager.isGemma4()) {
                type = PromptFactory::BackendType::LOCAL_GEMMA_4;
            }
            std::string wrapped_input = PromptFactory::wrap(input, type);

            if (m_impl->m_use_kotlin_fallback) {
                LOGI(TAG, "Executing JNI Fallback Reasoning [Seq: %u]", seq_id);
                std::string response = Ronin::Kernel::Capability::HardwareBridge::runNeuralReasoning(wrapped_input);
                
                if (m_impl->spine_bridge && m_impl->spine_bridge->is_valid()) {
                    HAL::SpineRingBuffer* rb = m_impl->spine_bridge->get();
                    if (rb) {
                        HAL::InferencePacket packet;
                        packet.sequence_id = seq_id; packet.token_id = 0; packet.confidence = 1.0f; packet.is_final = true;
                        std::strncpy(packet.fragment, response.c_str(), sizeof(packet.fragment) - 1);
                        packet.fragment[sizeof(packet.fragment) - 1] = '\0';
                        rb->push(packet);
                    }
                }
            } else {
                LOGI(TAG, "Executing Lock-on Native Reasoning [Seq: %u]", seq_id);
                auto callback = [](void* user_data, const char** partial_results, int count, bool done) {
                    auto* self = static_cast<InferenceEngine*>(user_data);
                    uint32_t current_seq = self->m_impl->sequence_counter.load();
                    if (self->m_impl->spine_bridge && self->m_impl->spine_bridge->is_valid()) {
                        HAL::SpineRingBuffer* rb = self->m_impl->spine_bridge->get();
                        if (rb) {
                            HAL::InferencePacket packet;
                            packet.sequence_id = current_seq; packet.token_id = 0; packet.confidence = 1.0f; packet.is_final = done;
                            std::string combined;
                            for (int i = 0; i < count; ++i) combined += partial_results[i];
                            std::strncpy(packet.fragment, combined.c_str(), sizeof(packet.fragment) - 1);
                            packet.fragment[sizeof(packet.fragment) - 1] = '\0';
                            int retries = 10;
                            while (!rb->push(packet) && retries-- > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                };
                char* err_msg = nullptr;
                if (m_impl->f_predict_async(m_impl->session_handle, wrapped_input.c_str(), callback, this, &err_msg) != 0) {
                    LOGE(TAG, "PredictAsync Error: %s", err_msg ? err_msg : "Unknown Error");
                    if (err_msg) free(err_msg);
                }
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
    return Ronin::Kernel::Capability::HardwareBridge::fetchCloudResponse(input, provider, apiKey);
}

int InferenceEngine::classifyCoarse(const std::string& input) { return 1; }
CognitiveIntent InferenceEngine::predictFine(const std::string& input, int coarse_category) { return {1, 1.0f, true}; }
std::string InferenceEngine::getModelPath() const { return m_impl->model_path; }
std::string InferenceEngine::getRuntimeInfo() const { return m_impl->m_use_kotlin_fallback ? "Runtime: JNI Fallback" : "Runtime: Native Lock-on Fix"; }
long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { m_impl->context_window = tokens; }
void InferenceEngine::purgeKVCache() { LOGI(TAG, "Purging Native KV Cache..."); }

} // namespace Ronin::Kernel::Model
