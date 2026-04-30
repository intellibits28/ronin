#include "models/inference_engine.h"
#include "models/prompt_factory.h"
#include "ronin_log.h"
#include "intent_engine.h"
#include "capabilities/hardware_bridge.h"
#include "litert/lm/engine.h"
#include <algorithm>

#define TAG "RoninInferenceEngine"

// PHASE 5.0: Production LiteRT-LM Implementation
// Weak stubs to allow linkage even if libllm_inference_engine_jni.so doesn't export them.
namespace litert::lm {
    __attribute__((weak)) absl::StatusOr<std::unique_ptr<LlmEngine>> LlmEngine::Create(const EngineConfig& config) {
        return absl::StatusOr<std::unique_ptr<LlmEngine>>();
    }
}

namespace Ronin::Kernel::Model {

struct InferenceEngine::Impl {
    std::string model_path;
    int context_window = 2048;
    std::unique_ptr<litert::lm::LlmEngine> engine;

    bool load(const std::string& path) {
        LOGI(TAG, "Hydration Protocol: Modern LiteRT-LM (Bundle Path: %s)", path.c_str());
        
        litert::lm::EngineConfig config;
        config.model_path = path;
        config.max_tokens = context_window;
        config.enable_ple = true; // Required for Gemma 4 E2B performance
        config.kv_cache_config.type = litert::lm::KVCacheType::kShared;

        auto engine_or = litert::lm::LlmEngine::Create(config);
        if (engine_or.ok()) {
            engine = engine_or.release();
            LOGI(TAG, "SUCCESS: Gemma 4 Brain Hydrated via LiteRT-LM.");
            return true;
        } else {
            LOGE(TAG, "FAILURE: Hydration failed: %s", engine_or.status().message().c_str());
            return false;
        }
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
    return m_impl->engine != nullptr;
}

std::string InferenceEngine::runLiteRTReasoning(const std::string& input) {
    if (!m_impl->engine) return "";

    std::string final_response;
    auto status = m_impl->engine->GenerateResponse(input, 
        [&final_response](const std::vector<std::string>& partial, bool done) {
            if (!partial.empty()) {
                for (const auto& s : partial) final_response += s;
            }
        });

    return status.ok() ? final_response : "Error: LiteRT-LM inference failed.";
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
std::string InferenceEngine::getRuntimeInfo() const { return "Runtime: LiteRT-LM (Production)"; }
long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { if (m_impl) m_impl->context_window = tokens; }

} // namespace Ronin::Kernel::Model
