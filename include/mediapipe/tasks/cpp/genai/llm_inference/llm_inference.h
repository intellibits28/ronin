#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

/**
 * PHASE 5.5: Production Header Alignment
 * This header contains DECLARATIONS for symbols that MUST be
 * resolved by libllm_inference_engine_jni.so.
 */

namespace absl {
    class Status {
    public:
        Status(); 
        bool ok() const; 
        std::string message() const;
    private:
        // Internal state is hidden in .so, but we need a placeholder
        // to avoid field access errors in stubs if used.
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

class LlmInference {
public:
    struct Options {
        std::string model_path;
        int max_tokens = 2048;
        int top_k = 40;
        float temperature = 0.7f;
        int random_seed = 42;
        float top_p = 1.0f;
        int lora_max_rank = 0;
    };

    // Symbol MUST be provided by libllm_inference_engine_jni.so
    static absl::StatusOr<std::unique_ptr<LlmInference>> Create(const Options& options);

    typedef std::function<void(const std::vector<std::string>&, bool)> ProgressCallback;

    // Symbol MUST be provided by libllm_inference_engine_jni.so
    absl::Status GenerateResponse(const std::string& prompt, ProgressCallback callback);
    
    virtual ~LlmInference() = default;
};

} // namespace mediapipe::tasks::genai::llm_inference
