#pragma once

#include <string>
#include <vector>
#include <memory>
#include "ronin_types.hpp"

namespace Ronin::Kernel::Model {

/**
 * Phase 4.3 (Updated): LiteRT-LM Integration.
 * Specialized Local Reasoning Spine using MediaPipe LLM Inference API.
 */
class InferenceEngine {
public:
    InferenceEngine(const std::string& model_path);
    ~InferenceEngine();

    /**
     * Layer 1 (Coarse): Quick classification into BROAD categories.
     */
    int classifyCoarse(const std::string& input);

    /**
     * Layer 2 (Fine): NPU-accelerated intent matching (NNAPI).
     */
    CognitiveIntent predictFine(const std::string& input, int coarse_category);

    /**
     * Phase 4.3: specialized LiteRT-LM Reasoning.
     * Uses MediaPipe LLM Inference API for autoregressive Gemma 4 decoding.
     * Implements native KV-cache management and prefill optimization.
     */
    std::string runLiteRTReasoning(const std::string& input);

    /**
     * Secure Cloud Escalation.
     * Triggered if local confidence is < 0.75.
     */
    std::string escalateToCloud(const std::string& input, const std::string& apiKey);

    /**
     * Data Protocol v4.3: Returns structured JSON for multi-turn reliability.
     */
    std::string getStructuredResponse(const std::string& intent, const std::string& state, const std::string& result);

    /**
     * Phase 4.4: Dynamic Model Loading.
     * Prioritizes user-selected path over default directory.
     */
    bool loadModel(const std::string& path);

    /**
     * Standard Intent Prediction (Legacy Wrapper).
     */
    CognitiveIntent predict(const std::string& input);

    /**
     * Power Management.
     */
    void suspendNPU();
    void resumeNPU();

    bool isLoaded() const;
    std::string getModelPath() const;
    std::string getRouterPath() const;
    std::string getRuntimeInfo() const;
    long verifyModel();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Ronin::Kernel::Model
