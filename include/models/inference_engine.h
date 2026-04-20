#pragma once

#include <string>
#include <vector>
#include <memory>
#include "ronin_types.hpp"

namespace Ronin::Kernel::Model {

/**
 * Phase 4.3: Hybrid Intelligence & External Brain.
 * Integrates LiteRT (Gemma 4) and Secure Cloud Escalation.
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
     * Layer 2 (Fine): NPU-accelerated intent matching.
     */
    CognitiveIntent predictFine(const std::string& input, int coarse_category);

    /**
     * Phase 4.3: External Local Brain (Gemma 4 + LiteRT).
     * Runs complex on-device reasoning if NPU intent matching is uncertain.
     */
    std::string runLocalReasoning(const std::string& input);

    /**
     * Phase 4.3: Cloud Escalation Bridge.
     * Escalates to Secure Cloud if Local Brain confidence is low.
     */
    std::string escalateToCloud(const std::string& input, const std::string& apiKey);

    /**
     * Data Protocol v4.3: Returns structured JSON instead of raw strings.
     */
    std::string getStructuredResponse(const std::string& intent, const std::string& state, const std::string& result);

    /**
     * Runs inference on the input text to determine the intent.
     */
    CognitiveIntent predict(const std::string& input);

    void suspendNPU();
    void resumeNPU();

    bool isLoaded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Ronin::Kernel::Model
