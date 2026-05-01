#include "models/inference_engine.h"
#include "models/prompt_factory.h"
#include "ronin_log.h"
#include "intent_engine.h"
#include "capabilities/hardware_bridge.h"
#include "mediapipe/tasks/cpp/genai/llm_inference/llm_inference.h"
#include <algorithm>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define TAG "RoninInferenceEngine"

using LlmInference = ::mediapipe::tasks::genai::llm_inference::LlmInference;
using LlmInferenceOptions = ::mediapipe::tasks::genai::llm_inference::LlmInferenceOptions;

/**
 * PHASE 5.9: Native Hydration Hardening (Zero-Copy)
 * Implements mmap-based model loading and dynamic symbol resolution.
 */

namespace Ronin::Kernel::Model {

typedef absl::StatusOr<std::unique_ptr<LlmInference>> (*LlmCreateFunc)(const LlmInferenceOptions&);

struct ModelMapper {
    void* data = MAP_FAILED;
    size_t size = 0;
    int fd = -1;

    bool map(const std::string& path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        
        struct stat sb;
        if (fstat(fd, &sb) < 0) {
            close(fd);
            return false;
        }
        size = sb.st_size;
        
        // Phase 5.9.1: Zero-Copy Memory Mapping
        data = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED) {
            close(fd);
            return false;
        }
        return true;
    }

    void unmap() {
        if (data != MAP_FAILED) munmap(data, size);
        if (fd >= 0) close(fd);
        data = MAP_FAILED;
        fd = -1;
    }

    ~ModelMapper() { unmap(); }
};

struct InferenceEngine::Impl {
    std::string model_path;
    int context_window = 2048;
    std::unique_ptr<LlmInference> engine;
    ModelMapper mapper;
    void* lib_handle = nullptr;
    LlmCreateFunc create_ptr = nullptr;

    ~Impl() {
        engine.reset(); // Release engine first
        mapper.unmap();
        if (lib_handle) dlclose(lib_handle);
    }

    bool load(const std::string& path) {
        LOGI(TAG, "Hydration Protocol: Native Hardening 5.9...");
        
#ifdef __ANDROID__
        // 1. Memory Map the .litertlm bundle
        if (!mapper.map(path)) {
            LOGE(TAG, "Memory Mapper: Failed to map bundle: %s", path.c_str());
            return false;
        }
        LOGI(TAG, "Memory Mapper: Successfully mapped %zu bytes (Zero-Copy).", mapper.size);

        // 2. Resolve Production Symbols
        if (!lib_handle) {
            lib_handle = dlopen("libllm_inference_engine_jni.so", RTLD_LAZY | RTLD_GLOBAL);
        }

        if (!lib_handle) {
            LOGE(TAG, "Dynamic Linker: Cannot open production library: %s", dlerror());
            return false;
        }

        const char* probes[] = {
            "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference17CreateFromOptionsERKNS2_19LlmInferenceOptionsE",
            "_ZN9mediapipe5tasks5genai13llm_inference12LlmInference17CreateFromOptionsERKNS3_7OptionsE"
        };

        for (const char* p : probes) {
            create_ptr = (LlmCreateFunc)dlsym(lib_handle, p);
            if (create_ptr) break;
        }

        if (!create_ptr) {
            LOGE(TAG, "FAILURE: Production 'CreateFromOptions' symbol not found.");
            return false;
        }

        // 3. Configure Production Options
        LlmInferenceOptions options;
        options.model_asset_buffer = static_cast<const char*>(mapper.data);
        options.model_asset_buffer_size = mapper.size;
        options.max_tokens = context_window;
        options.temperature = 0.7f;
        options.accel_type = LlmInferenceOptions::AccelType::GPU; // Force Adreno/Vulkan

        LOGI(TAG, "Invoking Production Engine Hydration (accel=GPU)...");
        auto engine_or = create_ptr(options);
        
        if (engine_or.ok()) {
            engine = engine_or.release();
            LOGI(TAG, "SUCCESS: Native reasoning spines hydrated.");
            return true;
        } else {
            LOGE(TAG, "FAILURE: Production hydration error: %s", engine_or.status().message().c_str());
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
    auto status = m_impl->engine->GenerateResponse(input, 
        [&final_response](const std::vector<std::string>& partial, bool done) {
            if (!partial.empty()) {
                for (const auto& s : partial) final_response += s;
            }
        });

    return status.ok() ? final_response : "Error: MediaPipe reasoning failed.";
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
std::string InferenceEngine::getRuntimeInfo() const { return "Runtime: LiteRT-LM (Hardened)"; }
long InferenceEngine::verifyModel() { return 100; }

void InferenceEngine::setContextWindow(int tokens) { if (m_impl) m_impl->context_window = tokens; }

void InferenceEngine::purgeKVCache() {
    if (m_impl && m_impl->engine) {
        LOGW(TAG, "Memory Pressure: Purging KV-Cache (Releasing Engine).");
        m_impl->engine.reset(); // Full release of NPU/GPU state
    }
}

} // namespace Ronin::Kernel::Model

