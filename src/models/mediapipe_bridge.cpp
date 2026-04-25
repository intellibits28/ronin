#include "mediapipe/tasks/cpp/genai/llm_inference/llm_inference.h"

/**
 * PHASE 4.8.1: MediaPipe Linkage Bridge
 * These are minimal implementations to satisfy the linker when the 
 * real MediaPipe library is not present in the build environment 
 * (e.g., GitHub Actions Host build).
 * 
 * In a real Android NDK production build, these symbols will be
 * overridden by the official .so libraries.
 */

namespace absl {
    bool Status::ok() const { return true; }
    std::string Status::message() const { return "Stub Status"; }
    
    Status OkStatus() { return Status(); }
}

namespace mediapipe::tasks::genai::llm_inference {

absl::StatusOr<std::unique_ptr<LlmInference>> LlmInference::Create(const Options& options) {
    // Return a null pointer to trigger the fallback/error logic in InferenceEngine
    return absl::StatusOr<std::unique_ptr<LlmInference>>();
}

absl::Status LlmInference::GenerateResponse(const std::string& prompt, ProgressCallback callback) {
    return absl::OkStatus();
}

} // namespace mediapipe::tasks::genai::llm_inference
