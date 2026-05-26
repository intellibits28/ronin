#include "models/inference_engine.h"
#include "models/prompt_factory.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include <thread>
#include <atomic>
#include <mutex>

#define TAG "RoninInference"

namespace Ronin::Kernel::Model {

struct InferenceEngine::Impl {
    std::string model_path;
    std::string base_path;
    int context_window = 2048;
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

    // Phase 11.0: Strictly Synchronous JNI Bridge (Freeze Prevention)
    LOGI(TAG, "Requesting Neural Reasoning via Sync JNI Bridge...");
    std::string result = Ronin::Kernel::Capability::HardwareBridge::runNeuralReasoning(wrapped_input);

    {
        std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
        m_impl->is_processing = false;
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
