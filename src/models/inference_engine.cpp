#include "models/inference_engine.h"
#include "hal/shared_memory_bridge.h"
#include "models/prompt_factory.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include <thread>
#include <atomic>
#include <mutex>

#define TAG "RoninInferenceProxy"

namespace Ronin::Kernel::Model {

/**
 * Phase 8.0: Microkernel Proxy
 * Instead of direct C++ execution, this proxy triggers the Kotlin Worker
 * and polls the SHM Ring Buffer for tokens.
 */
struct InferenceEngine::Impl {
    std::string model_path;
    std::string base_path;
    int context_window = 2048;
    
    std::unique_ptr<HAL::SharedMemoryBridge<HAL::SpineRingBuffer>> spine_bridge;
    
    std::atomic<uint32_t> sequence_counter{0};
    std::mutex inference_mutex;
    bool is_processing = false;

    Impl(const std::string& path) : model_path(path) {}

    bool initLlm() {
        // Highway remains C++ based for zero-lag streaming
        LOGI(TAG, "Initializing SHM Highway at: %s", base_path.c_str());
        spine_bridge = std::make_unique<HAL::SharedMemoryBridge<HAL::SpineRingBuffer>>("spine_stream");
        if (!spine_bridge->create(base_path, true)) {
            LOGE(TAG, "Failed to create SHM Spine Bridge");
            return false;
        }

        LOGI(TAG, "Microkernel Proxy Ready (Waiting for Inference requests)");
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
    // No longer needed for Microkernel Proxy
}

bool InferenceEngine::loadModel(const std::string& path) {
    m_impl->model_path = path;
    return m_impl->initLlm();
}

bool InferenceEngine::isLoaded() const {
    // Proxy is always 'ready' as long as the bridge is valid
    return m_impl->spine_bridge && m_impl->spine_bridge->is_valid();
}

std::string InferenceEngine::runLiteRTReasoning(const std::string& input) {
    if (!isLoaded()) return "Error: SHM Highway not initialized.";

    {
        std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
        if (m_impl->is_processing) return "Error: Engine Busy.";
        m_impl->is_processing = true;
    }

    uint32_t seq_id = ++m_impl->sequence_counter;
    
    // Detect Model Type for Prompt Formatting
    PromptFactory::BackendType type = PromptFactory::BackendType::LOCAL_GEMMA_2;
    if (m_impl->model_path.find("gemma-4") != std::string::npos || 
        m_impl->model_path.find(".litertlm") != std::string::npos) {
        type = PromptFactory::BackendType::LOCAL_GEMMA_4;
    }
    std::string wrapped_input = PromptFactory::wrap(input, type);

    // Microkernel Action: Trigger Kotlin Worker via JNI Proxy
    // We reuse HardwareBridge to communicate back to NativeEngine.kt
    LOGI(TAG, "Microkernel: Requesting Worker Inference [Seq: %u]", seq_id);
    
    // This is an asynchronous request. Kotlin will spawn the worker and start pushing to SHM.
    std::thread([this, wrapped_input]() {
        // Notify Kotlin to start the Lazy Worker
        Ronin::Kernel::Capability::HardwareBridge::runNeuralReasoning(wrapped_input);
        
        // Finalize C++ side processing state (Polling is handled by UI/Core separately)
        std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
        m_impl->is_processing = false;
    }).detach();

    return "Reasoning Started [SHM Active]";
}

std::string InferenceEngine::escalateToCloud(const std::string& input, const std::string& apiKey, const std::string& provider) {
    return Ronin::Kernel::Capability::HardwareBridge::fetchCloudResponse(input, provider, apiKey);
}

int InferenceEngine::classifyCoarse(const std::string& input) { return 1; }
CognitiveIntent InferenceEngine::predictFine(const std::string& input, int coarse_category) { return {1, 1.0f, true}; }
std::string InferenceEngine::getModelPath() const { return m_impl->model_path; }
std::string InferenceEngine::getRuntimeInfo() const { return "Runtime: Microkernel Proxy (SHM Mode)"; }
long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { m_impl->context_window = tokens; }
void InferenceEngine::purgeKVCache() { 
    LOGI(TAG, "Proxy: Requesting Worker RAM Trim...");
    // Future: Add callback to Kotlin for aggressive trimming
}

} // namespace Ronin::Kernel::Model
