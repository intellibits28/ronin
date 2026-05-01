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
 * PHASE 5.7: Multi-Level Symbol Probing (Final Linkage Resolve)
 * This layer uses dynamic discovery to find the exact mangled names in 
 * libllm_inference_engine_jni.so, bypassing static linkage limitations.
 */

namespace Ronin::Kernel::Model {

typedef absl::StatusOr<std::unique_ptr<LlmInference>> (*LlmCreateFunc)(const LlmInferenceOptions&);

struct InferenceEngine::Impl {
    std::string model_path;
    int context_window = 2048;
    std::unique_ptr<LlmInference> engine;
    void* lib_handle = nullptr;
    LlmCreateFunc create_ptr = nullptr;

    ~Impl() {
        if (lib_handle) dlclose(lib_handle);
    }

    bool load(const std::string& path) {
        LOGI(TAG, "Hydration Protocol: Probing Production Symbols...");
        
#ifdef __ANDROID__
        if (!lib_handle) {
            lib_handle = dlopen("libllm_inference_engine_jni.so", RTLD_LAZY | RTLD_GLOBAL);
        }

        if (!lib_handle) {
            LOGE(TAG, "Linkage FAILURE: Production .so not accessible: %s", dlerror());
            return false;
        }

        // Probing mangled names for LlmInference::Create
        const char* probes[] = {
            "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference6CreateERKNS2_19LlmInferenceOptionsE",
            "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference6CreateERKNS3_7OptionsE",
            "LlmInferenceCreate" // Possible C wrapper
        };

        for (const char* p : probes) {
            create_ptr = (LlmCreateFunc)dlsym(lib_handle, p);
            if (create_ptr) {
                LOGI(TAG, "Linkage SUCCESS: Found symbol: %s", p);
                break;
            }
        }

        if (!create_ptr) {
            LOGE(TAG, "Linkage FAILURE: No valid 'Create' symbol found. Reverting to Cloud-Only Reasoning.");
            return false;
        }

        LlmInferenceOptions options;
        options.model_path = path;
        options.max_tokens = context_window;
        options.temperature = 0.7f;
        options.top_k = 40;

        auto engine_or = create_ptr(options);
        if (engine_or.ok()) {
            engine = engine_or.release();
            LOGI(TAG, "SUCCESS: Gemma 4 Brain Hydrated.");
            return true;
        } else {
            LOGE(TAG, "FAILURE: Production library refused hydration (Internal Error).");
            return false;
        }
#else
        return true;
#endif
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
#ifdef __ANDROID__
    if (!m_impl->engine) return "";

    std::string final_response;
    // We attempt GenerateResponse - if linkage fails for this method, 
    // it will be caught by our weak stub logic in Phase 5.5.
    auto status = m_impl->engine->GenerateResponse(input, 
        [&final_response](const std::vector<std::string>& partial, bool done) {
            if (!partial.empty()) {
                for (const auto& s : partial) final_response += s;
            }
        });

    return status.ok() ? final_response : "";
#else
    return "Host Build: Reasoning mocked for input: " + input;
#endif
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
std::string InferenceEngine::getRuntimeInfo() const { return "Runtime: LiteRT-LM (Multi-Probe)"; }
long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { if (m_impl) m_impl->context_window = tokens; }

} // namespace Ronin::Kernel::Model
