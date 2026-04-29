#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

/**
 * PHASE 5.0: MediaPipe Production Header (Linker Bridge)
 * RULE 6: Zero-Mock Policy. 
 * This header contains DECLARATIONS ONLY. 
 * Implementation is provided by libllm_inference_engine_jni.so at runtime.
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
        StatusOr();
        StatusOr(T&& val);
        bool ok() const;
        T& operator*();
        T* operator->();
        Status status() const;
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

    static absl::StatusOr<std::unique_ptr<LlmInference>> Create(const Options& options);

    typedef std::function<void(const std::vector<std::string>&, bool)> ProgressCallback;

    absl::Status GenerateResponse(const std::string& prompt, ProgressCallback callback);
    
    virtual ~LlmInference() = default;
};

} // namespace mediapipe::tasks::genai::llm_inference
