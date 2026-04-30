#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

/**
 * PHASE 5.0: MediaPipe Production Header (Linker Bridge)
 * RULE 6: Zero-Mock Policy. 
 * This header contains DECLARATIONS for symbols that MUST be
 * resolved by libllm_inference_engine_jni.so.
 */

namespace absl {
    class Status {
    public:
        Status(); // Implementation in .so
        bool ok() const; // Implementation in .so
        std::string message() const; // Implementation in .so
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
    };

    // Symbol MUST be provided by libllm_inference_engine_jni.so
    static absl::StatusOr<std::unique_ptr<LlmInference>> Create(const Options& options);

    typedef std::function<void(const std::vector<std::string>&, bool)> ProgressCallback;

    // Symbol MUST be provided by libllm_inference_engine_jni.so
    absl::Status GenerateResponse(const std::string& prompt, ProgressCallback callback);
    
    virtual ~LlmInference() = default;
};

} // namespace mediapipe::tasks::genai::llm_inference
