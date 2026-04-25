#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

/**
 * PHASE 4.8.1: MediaPipe Production Header (Declarations Only)
 * RULE 6: Zero-Mock Policy. Function bodies removed to ensure
 * linkage with the real MediaPipe binary.
 */

namespace absl {
    class Status {
    public:
        bool ok() const;
        std::string message() const;
    };

    template <typename T>
    class StatusOr {
    public:
        bool ok() const;
        T& operator*();
        T* operator->();
        Status status() const;
    private:
        T value;
    };

    Status OkStatus();
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
};

} // namespace mediapipe::tasks::genai::llm_inference
