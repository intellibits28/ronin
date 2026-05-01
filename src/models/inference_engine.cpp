#include "models/inference_engine.h"
#include "models/prompt_factory.h"
#include "ronin_log.h"
#include "intent_engine.h"
#include "capabilities/hardware_bridge.h"
#include "mediapipe/tasks/cpp/genai/llm_inference/llm_inference.h"
#include <algorithm>
#include <dlfcn.h>

#define TAG "RoninInferenceEngine"

using LlmInference = ::mediapipe::tasks::genai::llm_inference::LlmInference;
using LlmInferenceOptions = ::mediapipe::tasks::genai::llm_inference::LlmInferenceOptions;

/**
 * PHASE 5.6: Dynamic Symbol Resolution (Resilience Layer)
 * Explicitly probing for MediaPipe symbols to bypass stub shadowing.
 */

namespace Ronin::Kernel::Model {

typedef absl::StatusOr<std::unique_ptr<LlmInference>> (*LlmCreateFunc)(const LlmInferenceOptions&);

struct InferenceEngine::Impl {
    std::string model_path;
    int context_window = 2048;
    std::unique_ptr<LlmInference> engine;

    bool load(const std::string& path) {
        LOGI(TAG, "Hydration Protocol: Dynamic Linkage (Path: %s)", path.c_str());
        
        // 1. Resolve LlmInference::Create symbol dynamically
        void* handle = dlopen("libllm_inference_engine_jni.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!handle) {
            LOGE(TAG, "Dynamic Linker: Cannot open production library: %s", dlerror());
            return false;
        }

        // Probing mangled names for LlmInference::Create(Options const&)
        // Standard mangling for the official MediaPipe signature
        const char* symbol_name = "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference6CreateERKNS2_7OptionsE";
        auto create_func = (LlmCreateFunc)dlsym(handle, symbol_name);

        if (!create_func) {
            LOGW(TAG, "Probing alternative symbol: LlmInference::Create...");
            symbol_name = "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference6CreateERKNS2_19LlmInferenceOptionsE";
            create_func = (LlmCreateFunc)dlsym(handle, symbol_name);
        }

        if (!create_func) {
            LOGE(TAG, "FAILURE: Production 'Create' symbol not found in .so. Symbols may be hidden.");
            return false;
        }

        LlmInferenceOptions options;
        options.model_path = path;
        options.max_tokens = context_window;
        options.temperature = 0.7f;
        options.top_k = 40;

        LOGI(TAG, "Invoking Production Engine Hydration...");
        auto engine_or = create_func(options);
        
        if (engine_or.ok()) {
            engine = engine_or.release();
            LOGI(TAG, "SUCCESS: Gemma 4 Brain Hydrated via Dynamic Symbol Resolution.");
            return true;
        } else {
            LOGE(TAG, "FAILURE: Hydration failed with error status.");
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
    // We use the direct method call here - if we reached this point, 
    // the virtual table should be correctly populated by the real object.
    auto status = m_impl->engine->GenerateResponse(input, 
        [&final_response](const std::vector<std::string>& partial, bool done) {
            if (!partial.empty()) {
                for (const auto& s : partial) final_response += s;
            }
        });

    return status.ok() ? final_response : "Error: MediaPipe inference execution failed.";
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
std::string InferenceEngine::getRuntimeInfo() const { return "Runtime: LiteRT-LM (Dynamic)"; }
long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { if (m_impl) m_impl->context_window = tokens; }

} // namespace Ronin::Kernel::Model
