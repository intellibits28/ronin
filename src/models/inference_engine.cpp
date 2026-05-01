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
 * PHASE 5.8: Full Dynamic Linkage (Zero-Link Policy)
 * This implementation resolves ALL MediaPipe symbols at runtime to avoid 
 * undefined symbol errors from hidden C++ exports in JNI libraries.
 */

namespace Ronin::Kernel::Model {

typedef absl::StatusOr<std::unique_ptr<LlmInference>> (*LlmCreateFunc)(const LlmInferenceOptions&);
typedef absl::Status (*LlmGenerateFunc)(LlmInference*, const std::string&, LlmInference::ProgressCallback);

struct InferenceEngine::Impl {
    std::string model_path;
    int context_window = 2048;
    std::unique_ptr<LlmInference> engine;
    void* lib_handle = nullptr;
    
    // Resolved function pointers
    LlmCreateFunc create_ptr = nullptr;
    LlmGenerateFunc generate_ptr = nullptr;

    ~Impl() {
        if (lib_handle) dlclose(lib_handle);
    }

    bool load(const std::string& path) {
        LOGI(TAG, "Hydration Protocol: Dynamic Linkage 5.8...");
        
#ifdef __ANDROID__
        if (!lib_handle) {
            lib_handle = dlopen("libllm_inference_engine_jni.so", RTLD_LAZY | RTLD_GLOBAL);
        }

        if (!lib_handle) {
            LOGE(TAG, "Linkage FAILURE: Production .so not accessible: %s", dlerror());
            return false;
        }

        // 1. Resolve 'Create' symbol
        const char* create_probes[] = {
            "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference6CreateERKNS2_19LlmInferenceOptionsE",
            "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference6CreateERKNS3_7OptionsE"
        };
        for (const char* p : create_probes) {
            create_ptr = (LlmCreateFunc)dlsym(lib_handle, p);
            if (create_ptr) break;
        }

        // 2. Resolve 'GenerateResponse' symbol
        const char* generate_probes[] = {
            "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference16GenerateResponseERKNSt3__112basic_stringIcNS4_11char_traitsIcEENS4_9allocatorIcEEEENS4_8functionIFvRKNS4_6vectorIS6_NS8_IS6_EEEEbEEE",
            "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference16GenerateResponseERKNS2_10StringViewE" // Alternative string view
        };
        for (const char* p : generate_probes) {
            generate_ptr = (LlmGenerateFunc)dlsym(lib_handle, p);
            if (generate_ptr) break;
        }

        if (!create_ptr) {
            LOGE(TAG, "Linkage FAILURE: 'Create' symbol not found.");
            return false;
        }

        LlmInferenceOptions options;
        options.model_path = path;
        options.max_tokens = context_window;

        auto engine_or = create_ptr(options);
        if (engine_or.ok()) {
            engine = engine_or.release();
            LOGI(TAG, "SUCCESS: Gemma 4 Brain Hydrated.");
            return true;
        } else {
            LOGE(TAG, "FAILURE: Production library refused hydration.");
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
    if (!m_impl->engine || !m_impl->generate_ptr) return "";

    std::string final_response;
    auto status = m_impl->generate_ptr(m_impl->engine.get(), input, 
        [&final_response](const std::vector<std::string>& partial, bool done) {
            if (!partial.empty()) {
                for (const auto& s : partial) final_response += s;
            }
        });

    return status.ok() ? final_response : "Error: Dynamic inference failed.";
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
std::string InferenceEngine::getRuntimeInfo() const { return "Runtime: LiteRT-LM (Full-Dynamic)"; }
long InferenceEngine::verifyModel() { return 100; }
void InferenceEngine::setContextWindow(int tokens) { if (m_impl) m_impl->context_window = tokens; }

} // namespace Ronin::Kernel::Model
