#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

/**
 * PHASE 5.9: Native Hydration Hardening (Production)
 * Aligned with official MediaPipe GenAI C++ API for SD778G+ GPU/NPU.
 */

namespace absl {
    class Status {
    public:
        Status();
        bool ok() const;
        std::string message() const;
    private:
        uintptr_t state_;
    };

    template <typename T>
    class StatusOr {
    public:
        StatusOr() : is_ok_(false) {}
        StatusOr(T&& val) : value_(std::move(val)), is_ok_(true) {}
        
        bool ok() const { return is_ok_; }
        const Status& status() const { return status_; }
        
        T& operator*() { return value_; }
        T* operator->() { return &value_; }
        T release() { return std::move(value_); }

    private:
        T value_;
        bool is_ok_;
        Status status_;
    };

    inline Status OkStatus() { return Status(); }
}

namespace mediapipe::tasks::genai::llm_inference {

struct LlmInferenceOptions {
    enum class AccelType : int {
        CPU = 0,
        GPU = 1,
        NPU = 2,
        VULKAN = 3
    };

    std::string model_path;
    const char* model_asset_buffer = nullptr;
    size_t model_asset_buffer_size = 0;
    
    int max_tokens = 2048;
    int top_k = 40;
    float temperature = 0.7f;
    int random_seed = 42;
    float top_p = 1.0f;
    
    AccelType accel_type = AccelType::GPU; // Force GPU/Vulkan for SD778G+
};

class LlmInference {
public:
    using Options = LlmInferenceOptions;

    // Symbol MUST be resolved via dlsym from libllm_inference_engine_jni.so
    static absl::StatusOr<std::unique_ptr<LlmInference>> CreateFromOptions(const Options& options);

    typedef std::function<void(const std::vector<std::string>&, bool)> ProgressCallback;

    virtual absl::Status GenerateResponse(const std::string& prompt, ProgressCallback callback) = 0;
    
    virtual ~LlmInference() = default;
};

} // namespace mediapipe::tasks::genai::llm_inference
