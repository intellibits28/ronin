#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

/**
 * PHASE 5.5: Production Header Alignment (Nested Struct Fix)
 * Aligned with official MediaPipe GenAI C++ API signatures.
 */

namespace absl {
    class Status {
    public:
        Status(); 
        bool ok() const; 
        std::string message() const;
    private:
        bool is_ok;
    };

    template <typename T>
    class StatusOr {
    public:
        StatusOr() : is_ok(false) {}
        StatusOr(T&& val) : value(std::move(val)), is_ok(true) {}
        bool ok() const { return is_ok; }
        T& operator*() { return value; }
        T* operator->() { return &value; }
        Status status() const { return status_; }
    private:
        T value;
        bool is_ok;
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
    // Signature: mediapipe::tasks::genai::llm_inference::LlmInference::Create(Options const&)
    static absl::StatusOr<std::unique_ptr<LlmInference>> Create(const Options& options);

    typedef std::function<void(const std::vector<std::string>&, bool)> ProgressCallback;

    // Symbol MUST be provided by libllm_inference_engine_jni.so
    absl::Status GenerateResponse(const std::string& prompt, ProgressCallback callback);
    
    virtual ~LlmInference() = default;
};

} // namespace mediapipe::tasks::genai::llm_inference
