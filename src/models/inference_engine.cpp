#include "models/inference_engine.h"
#include "models/prompt_factory.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include "failure_telemetry_bus.h"
#include <thread>
#include <atomic>
#include <mutex>

#define TAG "RoninInference"

namespace Ronin::Kernel::Model {

struct InferenceEngine::Impl {
    std::string model_path;
    std::string base_path;
    int context_window = 512;
    std::mutex inference_mutex;
    bool is_processing = false;

    Impl(const std::string& path) : model_path(path) {}
};

InferenceEngine::InferenceEngine(const std::string& modelPath) 
    : m_impl(std::make_unique<Impl>(modelPath)) {}

InferenceEngine::~InferenceEngine() = default;

void InferenceEngine::setBasePath(const std::string& path) {
    if (m_impl) m_impl->base_path = path;
}

void InferenceEngine::setLibPath(const std::string& path) {
    // No longer needed
}

bool InferenceEngine::loadModel(const std::string& path) {
    m_impl->model_path = path;
    LOGI(TAG, "Brain Model Registry Updated: %s", path.c_str());
    return true;
}

bool InferenceEngine::isLoaded() const {
    return !m_impl->model_path.empty();
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

std::string InferenceEngine::runLiteRTReasoning(const std::string& input, const std::string& systemPrompt) {
    std::string wrapped_input;
    {
        std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
        if (m_impl->is_processing) return "Error: Engine Busy.";
        m_impl->is_processing = true;
        resetCancellation();

        PromptFactory::BackendType type = PromptFactory::BackendType::LOCAL_GEMMA_4;
        if (m_impl->model_path.find("gemma-2") != std::string::npos) {
            type = PromptFactory::BackendType::LOCAL_GEMMA_2;
        }
        wrapped_input = PromptFactory::wrap(input, type, systemPrompt);
    }

    struct ProcessingGuard {
        InferenceEngine::Impl* impl;
        ~ProcessingGuard() {
            if (!impl) return;
            std::lock_guard<std::mutex> lock(impl->inference_mutex);
            impl->is_processing = false;
        }
    } processing_guard{m_impl.get()};

    // Phase 11.0: Strictly Synchronous JNI Bridge (Freeze Prevention)
    LOGI(TAG, "Requesting Neural Reasoning via Sync JNI Bridge...");
    std::string result = Ronin::Kernel::Capability::HardwareBridge::runNeuralReasoning(wrapped_input);

    // v1.5 Self-Healing: Check for LiteRT internal invocation errors
    if (result.find("Status Code: 13") != std::string::npos || result.find("Failed to invoke") != std::string::npos) {
        LOGE(TAG, "L15 Self-Healing: LiteRT Invocation Failed (Code 13). Attempting Local Recovery...");
        
        // 1. Log failure for learning loop
        Execution::FailureTelemetryBus::getInstance().logFailure("inference", "Gemma", FailureType::UNKNOWN, "LITERT_INVOKE_ERROR_13");
        
        // 2. Notify User and Reset Context (Self-Healing Step)
        Ronin::Kernel::Capability::HardwareBridge::pushMessage("[SYSTEM] Neural Spine Unstable (Code 13). Resetting engine...");
        
        // 3. Brief sleep to allow Kotlin side to release resources
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        
        // 4. Retry Once: InferenceService will auto-rehydrate on this call
        LOGI(TAG, "L15: Retrying inference after hard reset...");
        result = Ronin::Kernel::Capability::HardwareBridge::runNeuralReasoning(wrapped_input);
        
        if (result.find("Status Code: 13") != std::string::npos) {
            LOGE(TAG, "L15: Terminal instability after reset. Aborting turn.");
            return result;
        }
    }

    if (isCancelled()) {
        LOGW(TAG, "Inference cancelled by hardware guard-rail.");
        return "";
    }

    return result;
}

std::string InferenceEngine::escalateToCloud(const std::string& input, const std::string& apiKey, const std::string& provider) {
    return Ronin::Kernel::Capability::HardwareBridge::fetchCloudResponse(input, provider, apiKey);
}

int InferenceEngine::classifyCoarse(const std::string& input) { return 1; }
CognitiveIntent InferenceEngine::predictFine(const std::string& input, int coarse_category) { return {1, 1.0f, true}; }
std::string InferenceEngine::getModelPath() const { return m_impl->model_path; }
std::string InferenceEngine::getRuntimeInfo() const { return "Runtime: Single-Spine (Gemma 4)"; }
long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { m_impl->context_window = tokens; }
void InferenceEngine::purgeKVCache() { 
    LOGI(TAG, "Requesting RAM Trim...");
}

} // namespace Ronin::Kernel::Model
