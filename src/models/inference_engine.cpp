#include "models/inference_engine.h"
#include "models/prompt_factory.h"
#include "ronin_log.h"
#include "intent_engine.h"
#include "capabilities/hardware_bridge.h"
#include <algorithm>

#define TAG "RoninInferenceEngine"

/**
 * PHASE 6.0: Hybrid Inference Engine (Linker Resilience)
 * Reasoning is delegated back to Kotlin via the HardwareBridge to avoid 
 * hidden C++ symbol conflicts in MediaPipe's production libraries.
 */

namespace Ronin::Kernel::Model {

struct InferenceEngine::Impl {
    std::string model_path;
    int context_window = 2048;
    bool is_hydrated = false;

    bool load(const std::string& path) {
        LOGI(TAG, "Hydration Protocol: Hybrid Delegation (Model: %s)", path.c_str());
        // State tracking only. Real hydration happens in NativeEngine.loadModelAsync().
        is_hydrated = true; 
        return true;
    }
};

InferenceEngine::InferenceEngine(const std::string& modelPath) : m_impl(std::make_unique<Impl>()) {
    m_impl->model_path = modelPath;
}

InferenceEngine::~InferenceEngine() = default;

bool InferenceEngine::loadModel(const std::string& path) {
    return m_impl->load(path);
}

bool InferenceEngine::isLoaded() const {
    return m_impl->is_hydrated;
}

std::string InferenceEngine::runLiteRTReasoning(const std::string& input) {
    // Phase 6.0: Cross-bridge delegation to Kotlin-owned LlmInference
    return Ronin::Kernel::Capability::HardwareBridge::runNeuralReasoning(input);
}

std::string InferenceEngine::escalateToCloud(const std::string& input, const std::string& apiKey, const std::string& provider) {
    return Ronin::Kernel::Capability::HardwareBridge::fetchCloudResponse(input, provider);
}

int InferenceEngine::classifyCoarse(const std::string& input) { return 1; }

CognitiveIntent InferenceEngine::predictFine(const std::string& input, int coarse_category) {
    std::string s = input;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    bool isOff = (s.find("off") != std::string::npos || s.find("stop") != std::string::npos || s.find("disable") != std::string::npos);
    CognitiveIntent intent = {1, 1.0f, true};
    if (s.find("light") != std::string::npos || s.find("torch") != std::string::npos) intent = {4, 1.0f, !isOff};
    else if (s.find("gps") != std::string::npos || s.find("location") != std::string::npos || s.find("ရောက်") != std::string::npos) intent = {5, 1.0f, true};
    else if (s.find("search") != std::string::npos || s.find("find") != std::string::npos || s.find("ရှာ") != std::string::npos) intent = {2, 1.0f, true};
    return intent;
}

std::string InferenceEngine::getModelPath() const { return m_impl->model_path; }
std::string InferenceEngine::getRuntimeInfo() const { return "Runtime: Hybrid (Kotlin-Gemma)"; }
long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { if (m_impl) m_impl->context_window = tokens; }
void InferenceEngine::purgeKVCache() {
    // In Hybrid mode, cache management is handled by Kotlin onTrimMemory hooks.
}

} // namespace Ronin::Kernel::Model
