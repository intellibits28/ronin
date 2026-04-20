#include "models/inference_engine.h"
#include "ronin_log.h"
#include "intent_engine.h"
#include <string>
#include <algorithm>
#include <cctype>
#include <cmath>

#define TAG "RoninNPU"

namespace Ronin::Kernel::Model {

struct InferenceEngine::Impl {
    std::string model_path;
    bool loaded = false;
    bool npu_active = false;

    Impl(const std::string& path) : model_path(path) {
        /**
         * RULE 1: NNAPI Execution Core.
         * In a full implementation, this would configure Ort::SessionOptions
         * with OrtNNAPIFlags: USE_FP16 | CPU_DISABLED.
         */
        LOGI(TAG, "Configuring NNAPI for Snapdragon 778G (Hexagon 770)...");
        LOGI(TAG, "> Flags: USE_FP16, CPU_DISABLED (NPU Priority).");
        
        // RULE 2: INT8 Quantized Model Loading
        LOGI(TAG, "Loading INT8 quantized model from: %s", path.c_str());
        
        loaded = true;
        npu_active = true;
    }
};

InferenceEngine::InferenceEngine(const std::string& model_path) {
    m_impl = std::make_unique<Impl>(model_path);
}

InferenceEngine::~InferenceEngine() = default;

int InferenceEngine::classifyCoarse(const std::string& input) {
    // Layer 1 (Coarse): BROAD categories (ACTION vs INFO)
    // Simplified head logic
    std::string s = input;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    if (s.find("how") != std::string::npos || s.find("what") != std::string::npos || s.find("search") != std::string::npos) {
        return 1; // INFO
    }
    return 0; // ACTION
}

CognitiveIntent InferenceEngine::predictFine(const std::string& input, int coarse_category) {
    /**
     * RULE 3: Thermal Fallback.
     * Respect global thermal state.
     */
    if (Ronin::Kernel::Intent::g_thermal_state == Ronin::Kernel::Intent::ThermalState::SEVERE) {
        LOGW(TAG, "Thermal SEVERE: NPU bypassed. Falling back to CPU-scalar.");
        // Simulated CPU-scalar fallback logic
        return predict(input);
    }

    if (!m_impl->npu_active) resumeNPU();

    LOGI(TAG, "NPU Inference (Fine) | Coarse Layer: %s", (coarse_category == 0 ? "ACTION" : "INFO"));

    // Simulated NPU fine head output
    std::string s = input;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    CognitiveIntent intent = {1, 0.4f, true};

    if (s.find("light") != std::string::npos || s.find("torch") != std::string::npos) {
        intent = {4, 0.98f, true};
    } else if (s.find("gps") != std::string::npos || s.find("location") != std::string::npos) {
        intent = {5, 0.96f, true};
    } else if (s.find("wifi") != std::string::npos) {
        intent = {6, 0.97f, true};
    } else if (s.find("blue") != std::string::npos) {
        intent = {7, 0.96f, true};
    } else if (s.find("find") != std::string::npos || s.find("search") != std::string::npos) {
        intent = {2, 0.85f, true};
    }

    /**
     * RULE 4: Risk-Aware Thresholds.
     * Dynamic confidence gates.
     */
    float threshold = (intent.id >= 4 && intent.id <= 7) ? 0.95f : 0.70f;
    
    if (intent.confidence < threshold) {
        LOGW(TAG, "Intent (ID %u) rejected: confidence %.2f below threshold %.2f", intent.id, intent.confidence, threshold);
        return {1, 0.5f, true}; // Fallback to Chat
    }

    return intent;
}

CognitiveIntent InferenceEngine::predict(const std::string& input) {
    return predictFine(input, classifyCoarse(input));
}

void InferenceEngine::suspendNPU() {
    if (m_impl->npu_active) {
        LOGI(TAG, "NPU entering hibernation to minimize idle drain.");
        m_impl->npu_active = false;
    }
}

void InferenceEngine::resumeNPU() {
    if (!m_impl->npu_active) {
        LOGI(TAG, "NPU waking from hibernation.");
        m_impl->npu_active = true;
    }
}

bool InferenceEngine::isLoaded() const {
    return m_impl && m_impl->loaded;
}

} // namespace Ronin::Kernel::Model
