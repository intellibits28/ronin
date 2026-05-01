#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

/**
 * PHASE 5.6: Production Header Alignment
 * Aligned with official MediaPipe GenAI C++ API signatures.
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
    std::string model_path;
    int max_tokens = 2048;
    int top_k = 40;
    float temperature = 0.7f;
    int random_seed = 42;
    float top_p = 1.0f;
    int lora_max_rank = 0;
};

class LlmInference {
public:
    using Options = LlmInferenceOptions;

    // Symbol MUST be provided by libllm_inference_engine_jni.so
    static absl::StatusOr<std::unique_ptr<LlmInference>> Create(const Options& options);

    typedef std::function<void(const std::vector<std::string>&, bool)> ProgressCallback;

    // Symbol MUST be provided by libllm_inference_engine_jni.so
    absl::Status GenerateResponse(const std::string& prompt, ProgressCallback callback);
    
    virtual ~LlmInference() = default;
};

} // namespace mediapipe::tasks::genai::llm_inference
