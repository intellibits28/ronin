#pragma once

#include <string>
#include <vector>
#include <memory>
#include "ronin_types.hpp"

namespace Ronin::Kernel::Model {

/**
 * Phase 4.2: NPU Integration via NNAPI.
 * High-performance intent routing targeting Snapdragon 778G (Hexagon 770).
 */
class InferenceEngine {
public:
    InferenceEngine(const std::string& model_path);
    ~InferenceEngine();

    /**
     * Layer 1 (Coarse): Quick classification into BROAD categories.
     * @return 0 for ACTION, 1 for INFO.
     */
    int classifyCoarse(const std::string& input);

    /**
     * Layer 2 (Fine): Detailed node matching using NPU-accelerated ONNX model.
     * Implements dynamic confidence gates and risk-aware thresholds.
     */
    CognitiveIntent predictFine(const std::string& input, int coarse_category);

    /**
     * Runs inference on the input text to determine the intent.
     * (Legacy wrapper for backward compatibility)
     */
    CognitiveIntent predict(const std::string& input);

    /**
     * NPU Hibernation: Release/Suspend handles during inactivity.
     */
    void suspendNPU();
    void resumeNPU();

    bool isLoaded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Ronin::Kernel::Model
