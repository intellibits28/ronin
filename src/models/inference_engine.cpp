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

void InferenceEngine::requestCancellation() {
    m_cancel_flag.store(true, std::memory_order_release);
}

void InferenceEngine::resetCancellation() {
    m_cancel_flag.store(false, std::memory_order_release);
}

bool InferenceEngine::isCancelled() const {
    return m_cancel_flag.load(std::memory_order_acquire);
}

std::string InferenceEngine::runLiteRTReasoning(const std::string& input) {
    if (!isLoaded()) return "Error: SHM Highway not initialized.";

    {
        std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
        if (m_impl->is_processing) return "Error: Engine Busy.";
        m_impl->is_processing = true;
    }

    resetCancellation();
    std::string response_buffer;

    // Phase 2: Hardware Guard-rail - Reset the SHM tail to clear stale tokens
    auto* rb = m_impl->spine_bridge->get();
    if (rb) {
        rb->tail.store(rb->head.load(std::memory_order_relaxed), std::memory_order_release);
    }

    // Phase 4: Wrap input with System Prompt and Tool Schemas
    PromptFactory::BackendType type = PromptFactory::BackendType::LOCAL_GEMMA_4;
    if (m_impl->model_path.find("gemma-2") != std::string::npos) {
        type = PromptFactory::BackendType::LOCAL_GEMMA_2;
    }
    std::string wrapped_input = PromptFactory::wrap(input, type);

    // Microkernel Action: Trigger Kotlin Worker via JNI Proxy
    LOGI(TAG, "Microkernel: Triggering Gemma 4 Inference...");
    Ronin::Kernel::Capability::HardwareBridge::runNeuralReasoning(wrapped_input);

    // Phase 2: Synchronous Polling Loop with Per-Token Cancellation
    bool done = false;
    while (!done) {
        // 1. High-frequency cancellation check (Relaxed order)
        if (m_cancel_flag.load(std::memory_order_relaxed)) {
            // 2. Cancellation exit (Acquire order to sync state)
            if (m_cancel_flag.load(std::memory_order_acquire)) {
                LOGI(TAG, "Inference Loop Aborted: Hardware Guard-rail triggered.");
                response_buffer.clear(); // Requirement 3: Discard partial content
                break;
            }
        }

        HAL::InferencePacket packet;
        if (rb && rb->pop(packet)) {
            response_buffer += packet.fragment;
            if (packet.is_final) {
                done = true;
            }
        } else {
            // Small yield to prevent CPU thrashing while waiting for NPU/GPU
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
        m_impl->is_processing = false;
    }

    return isCancelled() ? "" : response_buffer;
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
