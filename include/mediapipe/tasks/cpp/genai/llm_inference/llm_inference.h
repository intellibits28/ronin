#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

/**
 * PHASE 4.8.1: MediaPipe Production Header (Compilation Bridge)
 * RULE 6: Zero-Mock Policy. 
 * This header contains declarations for core methods to ensure
 * they are resolved by the production .so library at runtime.
 */

namespace absl {
    class Status {
    public:
        Status() : is_ok(true) {}
        bool ok() const { return is_ok; }
        // Declaration only - implementation must come from the .so
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
        // Returns a Status object (must be provided by .so)
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

    // DECLARATION ONLY: Must be implemented by production library
    static absl::StatusOr<std::unique_ptr<LlmInference>> Create(const Options& options);

    typedef std::function<void(const std::vector<std::string>&, bool)> ProgressCallback;

    // DECLARATION ONLY: Must be implemented by production library
    absl::Status GenerateResponse(const std::string& prompt, ProgressCallback callback);
    
    virtual ~LlmInference() = default;
};

} // namespace mediapipe::tasks::genai::llm_inference
