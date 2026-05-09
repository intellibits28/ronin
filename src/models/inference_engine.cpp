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

// --- Stable Flat C API Definition ---
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
typedef void (*LlmDestroyEngineFunc)(LlmInferenceEngineHandle);
typedef void (*LlmDestroySessionFunc)(LlmInferenceSessionHandle);

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
    LlmDestroyEngineFunc f_destroy_engine = nullptr;
    LlmDestroySessionFunc f_destroy_session = nullptr;

    bool m_use_kotlin_fallback = false;

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

        lib_handle = dlopen(nullptr, RTLD_NOW);
        if (!lib_handle) {
            LOGE(TAG, "dlopen(nullptr) failed: %s", dlerror());
            return false;
        }

        LOGI(TAG, "Commencing Exhaustive Symbol Probe (Master Probe Array)...");

        auto probe = [&](const char* base_name) -> void* {
            const char* prefixes[] = {"LlmInferenceEngine_", "LlmInference_", "LlmEngine_", ""};
            for (const char* pref : prefixes) {
                std::string full_name = std::string(pref) + base_name;
                void* sym = dlsym(lib_handle, full_name.c_str());
                if (sym) return sym;
                std::string underscore = "_" + full_name;
                sym = dlsym(lib_handle, underscore.c_str());
                if (sym) return sym;
            }
            return nullptr;
        };

        f_create_engine = (LlmCreateEngineFunc)probe("CreateEngine");
        if (!f_create_engine) f_create_engine = (LlmCreateEngineFunc)probe("Create");

        f_create_session = (LlmCreateSessionFunc)probe("CreateSession");
        f_predict_async = (LlmPredictAsyncFunc)probe("PredictAsync");
        f_destroy_engine = (LlmDestroyEngineFunc)probe("DestroyEngine");
        f_destroy_session = (LlmDestroySessionFunc)probe("DestroySession");

        if (f_create_engine && f_create_session && f_predict_async) {
            LOGI(TAG, "SUCCESS: Stable C API symbols resolved from memory.");
            LlmModelSettings settings = { .model_path = model_path.c_str(), .cache_dir = base_path.c_str(), .max_num_tokens = context_window, .preferred_backend = 0 };
            char* err_msg = nullptr;
            if (f_create_engine(&settings, &engine_handle, &err_msg) != 0) {
                LOGE(TAG, "Native Engine Creation failed: %s. Enabling Kotlin Fallback.", err_msg ? err_msg : "Unknown Error");
                if (err_msg) free(err_msg);
                m_use_kotlin_fallback = true;
            }
            if (!m_use_kotlin_fallback && f_create_session(engine_handle, nullptr, &session_handle, &err_msg) != 0) {
                LOGE(TAG, "Native Session Creation failed: %s. Enabling Kotlin Fallback.", err_msg ? err_msg : "Unknown Error");
                if (err_msg) free(err_msg);
                m_use_kotlin_fallback = true;
            }
        } else {
            LOGW(TAG, "Native C API NOT FOUND. Activating Track 2: JNI Fallback (Kotlin Delegation).");
            m_use_kotlin_fallback = true;
        }

        spine_bridge = std::make_unique<HAL::SharedMemoryBridge<HAL::SpineRingBuffer>>("spine_stream");
        if (!spine_bridge->create(base_path, true)) {
            LOGE(TAG, "Failed to create SHM Spine Bridge at %s", base_path.c_str());
            return false;
        }

        LOGI(TAG, "Inference Spine Ready (Mode: %s)", m_use_kotlin_fallback ? "Kotlin Fallback" : "Pure Native");
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
            if (m_impl->model_path.find("gemma-4") != std::string::npos || m_impl->model_path.find(".litertlm") != std::string::npos) {
                type = PromptFactory::BackendType::LOCAL_GEMMA_4;
            }
            std::string wrapped_input = PromptFactory::wrap(input, type);

            if (m_impl->m_use_kotlin_fallback) {
                LOGI(TAG, "Executing JNI Fallback Reasoning [Seq: %u]", seq_id);
                // Call back to Kotlin via HardwareBridge
                std::string response = Ronin::Kernel::Capability::HardwareBridge::runNeuralReasoning(wrapped_input);
                
                // Stream response into SHM even in fallback mode for UI consistency
                if (m_impl->spine_bridge) {
                    HAL::SpineRingBuffer* rb = m_impl->spine_bridge->get();
                    if (rb) {
                        HAL::InferencePacket packet;
                        packet.sequence_id = seq_id;
                        packet.token_id = 0;
                        packet.confidence = 1.0f;
                        packet.is_final = true;
                        std::strncpy(packet.fragment, response.c_str(), sizeof(packet.fragment) - 1);
                        packet.fragment[sizeof(packet.fragment) - 1] = '\0';
                        rb->push(packet);
                    }
                }
            } else {
                LOGI(TAG, "Executing Stable Native Reasoning [Seq: %u]", seq_id);
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
std::string InferenceEngine::getRuntimeInfo() const { return m_impl->m_use_kotlin_fallback ? "Runtime: JNI Fallback" : "Runtime: Native Flat-C API"; }
long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { m_impl->context_window = tokens; }
void InferenceEngine::purgeKVCache() { LOGI(TAG, "Purging Native KV Cache..."); }

} // namespace Ronin::Kernel::Model
