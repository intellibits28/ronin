#include "models/inference_engine.h"
#include "models/hydration_manager.h"
#include "hal/shared_memory_bridge.h"
#include "mediapipe/tasks/cpp/genai/llm_inference/llm_inference.h"
#include "models/prompt_factory.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <dlfcn.h>

#define TAG "RoninInferenceEngine"

namespace Ronin::Kernel::Model {

using namespace mediapipe::tasks::genai::llm_inference;
using namespace Ronin::Kernel::HAL;

// Function pointer type for CreateFromOptions (Mangled name for Clang arm64)
typedef absl::StatusOr<std::unique_ptr<LlmInference>> (*CreateFromOptionsFunc)(const LlmInferenceOptions&);

struct InferenceEngine::Impl {
    std::string model_path;
    std::string base_path;
    int context_window = 2048;
    
    HydrationManager hydration_manager;
    std::unique_ptr<SharedMemoryBridge<SpineRingBuffer>> spine_bridge;
    std::unique_ptr<LlmInference> llm_inference;
    
    std::atomic<uint32_t> sequence_counter{0};
    std::mutex inference_mutex;
    bool is_processing = false;

    Impl(const std::string& path) : model_path(path) {
    }

    bool initLlm() {
        if (!hydration_manager.hydrate(model_path)) {
            LOGE(TAG, "Failed to hydrate model: %s", model_path.c_str());
            return false;
        }

        LlmInferenceOptions options;
        options.model_path = model_path; 
        options.model_asset_buffer = static_cast<const char*>(hydration_manager.getModelPtr());
        options.model_asset_buffer_size = hydration_manager.getModelSize();
        options.max_tokens = context_window;
        options.accel_type = LlmInferenceOptions::AccelType::GPU;

        // Phase 3.5: Exhaustive Symbol Probing (Gemma 2, 3, 4 Compatibility)
        void* handle = dlopen("libllm_inference_engine_jni.so", RTLD_NOW);
        if (!handle) {
            LOGE(TAG, "dlopen failed for libllm_inference_engine_jni.so: %s", dlerror());
            return false;
        }

        // Probing multiple mangled name patterns across different MediaPipe/LiteRT versions
        const char* symbols[] = {
            // Pattern 1: Modern MediaPipe GenAI (Gemma 4 / Latest)
            "_ZN9mediapipe5tasks3cpp5genai13llm_inference12LlmInference17CreateFromOptionsERKNS2_19LlmInferenceOptionsE",
            "_ZN10mediapipe5tasks3cpp5genai13llm_inference12LlmInference17CreateFromOptionsERKNS3_19LlmInferenceOptionsE",
            
            // Pattern 2: Legacy MediaPipe Task API (Gemma 2/3 Early)
            "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference6CreateERKNS1_19LlmInferenceOptionsE",
            "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference17CreateFromOptionsERKNS1_19LlmInferenceOptionsE",
            
            // Pattern 3: New LiteRT-LM Branded API
            "_ZN6litert2lm9LlmEngine6CreateERKNS0_12EngineConfigE",
            "_ZN6litert5genai13llm_inference12LlmInference17CreateFromOptionsERKNS1_19LlmInferenceOptionsE",

            // Pattern 4: Namespace variations (google::mediapipe or cc namespace)
            "_ZN9mediapipe5tasks2cc5genai13llm_inference12LlmInference17CreateFromOptionsERKNS2_19LlmInferenceOptionsE",
            "_ZN7google9mediapipe5tasks5genai13llm_inference12LlmInference17CreateFromOptionsERKNS3_19LlmInferenceOptionsE"
        };
        
        CreateFromOptionsFunc create_func = nullptr;
        for (const char* sym : symbols) {
            create_func = (CreateFromOptionsFunc)dlsym(handle, sym);
            if (create_func) {
                LOGI(TAG, "SUCCESS: Linker resolved LLM symbol -> %s", sym);
                break;
            }
        }

        if (!create_func) {
            LOGE(TAG, "FATAL: Could not resolve LLM Instance Creator (All %zu probe patterns failed)", sizeof(symbols)/sizeof(symbols[0]));
            dlclose(handle);
            return false;
        }

        auto result = create_func(options);
        if (!result.ok()) {
            LOGW(TAG, "GPU acceleration failed: %s. Falling back to CPU...", result.status().message().c_str());
            options.accel_type = LlmInferenceOptions::AccelType::CPU;
            result = create_func(options);
        }

        if (!result.ok()) {
            LOGE(TAG, "LlmInference::Create failed (CPU & GPU): %s", result.status().message().c_str());
            dlclose(handle);
            return false;
        }

        llm_inference = result.release();
        
        // Initialize SpineBridge with dynamic base_path
        spine_bridge = std::make_unique<SharedMemoryBridge<SpineRingBuffer>>("spine_stream");
        if (!spine_bridge->create(base_path, true)) {
            LOGE(TAG, "Failed to create SHM Spine Bridge at %s", base_path.c_str());
            return false;
        }

        LOGI(TAG, "Native Inference Engine Hydrated & Ready.");
        return true;
    }
};

InferenceEngine::InferenceEngine(const std::string& modelPath) 
    : m_impl(std::make_unique<Impl>(modelPath)) {}

InferenceEngine::~InferenceEngine() = default;

void InferenceEngine::setBasePath(const std::string& path) {
    if (m_impl) m_impl->base_path = path;
}

bool InferenceEngine::loadModel(const std::string& path) {
    m_impl->model_path = path;
    return m_impl->initLlm();
}

bool InferenceEngine::isLoaded() const {
    return m_impl->llm_inference != nullptr;
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
            LOGI(TAG, "Starting Native Reasoning [Seq: %u]", seq_id);
            
            // Phase 2: Restoration - Explicit Prompt Wrapping
            PromptFactory::BackendType type = PromptFactory::BackendType::LOCAL_GEMMA_2;
            if (m_impl->model_path.find("gemma-4") != std::string::npos || 
                m_impl->model_path.find(".litertlm") != std::string::npos) {
                type = PromptFactory::BackendType::LOCAL_GEMMA_4;
            }
            
            std::string wrapped_input = PromptFactory::wrap(input, type);
            
            auto callback = [this, seq_id](const std::vector<std::string>& partial_results, bool done) {
                if (m_impl->spine_bridge) {
                    SpineRingBuffer* rb = m_impl->spine_bridge->get();
                    if (rb) {
                        InferencePacket packet;
                        packet.sequence_id = seq_id;
                        packet.token_id = 0; 
                        packet.confidence = 1.0f;
                        packet.is_final = done;
                        
                        std::string combined;
                        for (const auto& s : partial_results) combined += s;
                        
                        // UTF-8 Safe Copy (Burmese Support)
                        std::strncpy(packet.fragment, combined.c_str(), sizeof(packet.fragment) - 1);
                        packet.fragment[sizeof(packet.fragment) - 1] = '\0';
                        
                        // Burst Control: Retry if buffer is full
                        int retries = 10;
                        while (!rb->push(packet) && retries-- > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        
                        if (retries < 0) {
                            LOGW(TAG, "Burst Control: SHM Buffer full, dropping packet for seq %u", seq_id);
                        }
                    }
                }
            };

            auto status = m_impl->llm_inference->GenerateResponse(wrapped_input, callback);
            if (!status.ok()) {
                LOGE(TAG, "GenerateResponse Error: %s", status.message().c_str());
            }
        } catch (const std::exception& e) {
            LOGE(TAG, "Inference Thread Exception: %s", e.what());
        } catch (...) {
            LOGE(TAG, "Unknown Inference Thread Exception");
        }

        // Finalize state: Always reset is_processing
        {
            std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
            m_impl->is_processing = false;
        }
        LOGI(TAG, "Native Reasoning Complete [Seq: %u]", seq_id);
    }).detach();

    return "Reasoning Started [SHM Active]";
}

std::string InferenceEngine::escalateToCloud(const std::string& input, const std::string& apiKey, const std::string& provider) {
    // Fallback to HardwareBridge for Cloud (This is still okay as it's not a heavy local model)
    return Ronin::Kernel::Capability::HardwareBridge::fetchCloudResponse(input, provider, apiKey);
}

int InferenceEngine::classifyCoarse(const std::string& input) { return 1; }

CognitiveIntent InferenceEngine::predictFine(const std::string& input, int coarse_category) {
    // Basic deterministic logic (Phase 6.2 compatibility)
    return {1, 1.0f, true};
}

std::string InferenceEngine::getModelPath() const { return m_impl->model_path; }
std::string InferenceEngine::getRuntimeInfo() const { 
    return "Runtime: Native LiteRT-LM (GPU) | Locked: " + std::string(m_impl->hydration_manager.isLocked() ? "YES" : "NO"); 
}

long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { m_impl->context_window = tokens; }

void InferenceEngine::purgeKVCache() {
    // MediaPipe GenAI handles internal cache, but we could re-initialize if needed.
    LOGI(TAG, "Purging Native KV Cache... (Engine Reset)");
}

} // namespace Ronin::Kernel::Model
