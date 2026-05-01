#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

/**
 * PHASE 5.4: Production Header Alignment
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

struct LlmInferenceOptions {
    std::string model_path;
    int max_tokens = 2048;
    int top_k = 40;
    float temperature = 0.7f;
    int random_seed = 42;
    float top_p = 1.0f;
};

class LlmInference {
public:
    // Production Signature
    static absl::StatusOr<std::unique_ptr<LlmInference>> Create(const LlmInferenceOptions& options);

    typedef std::function<void(const std::vector<std::string>&, bool)> ProgressCallback;

    absl::Status GenerateResponse(const std::string& prompt, ProgressCallback callback);
    
    virtual ~LlmInference() = default;
};

} // namespace mediapipe::tasks::genai::llm_inference
