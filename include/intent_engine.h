#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "ronin_types.hpp"
#include "models/inference_engine.h"
#include "capabilities/base_skill.h"
#include "checkpoint_manager.h"
#include "lora_engine.h"

namespace Ronin::Kernel::Intent {

enum class ThermalState {
    NORMAL,
    MODERATE,
    SEVERE
};

// Global thermal state (should be updated by Android PowerManager/Thermal HAL)
extern ThermalState g_thermal_state;

/**
 * Calculates cosine similarity between two 128-dim INT8 pre-normalized vectors.
 * Uses NEON SIMD with thermal-aware fallback.
 */
float compute_intent_similarity_neon(const int8_t* a, const int8_t* b);

/**
 * Calculates cosine similarity between two float vectors.
 * Optimized with NEON SIMD for mobile performance.
 */
float compute_cosine_similarity_neon(const float* a, const float* b, size_t length);

class IntentEngine {
public:
    IntentEngine();

    /**
     * Loads capability manifest from a JSON-like formatted file.
     */
    void loadCapabilities(const std::string& json_path);

    /**
     * Attaches an ONNX inference engine for Tier 3 detection.
     */
    void setInferenceEngine(std::unique_ptr<Model::InferenceEngine> engine) {
        m_inference_engine = std::move(engine);
    }

    /**
     * Processes raw input to determine the high-level intent score.
     */
    CognitiveIntent process(const std::string& input, const std::string& context_subject = "");

    /**
     * Phase 4.0: Vtable-based Skill Execution
     * @param nodeId The target node ID from the Reasoning Spine.
     * @param param The extracted parameter for this tool.
     * @return A response string for the UI.
     */
    std::string executeSkill(uint32_t nodeId, const std::string& param);

    /**
     * Checks if a modular skill is registered for the given ID.
     */
    bool hasSkill(uint32_t nodeId) const {
        return m_skill_registry.find(nodeId) != m_skill_registry.end();
    }

    /**
     * Registers an externally managed skill (e.g. FileSearchNode).
     */
    void registerSkill(uint32_t id, std::shared_ptr<Ronin::Kernel::Capability::BaseSkill> skill) {
        m_skill_registry[id] = skill;
    }

    /**
     * Attaches a checkpoint manager for survival core.
     */
    void setCheckpointManager(std::shared_ptr<Ronin::Kernel::Checkpoint::CheckpointManager> cm) {
        m_checkpoint_manager = cm;
    }

    /**
     * @return The attached checkpoint manager.
     */
    std::shared_ptr<Ronin::Kernel::Checkpoint::CheckpointManager> getCheckpointManager() const {
        return m_checkpoint_manager;
    }

    /**
     * Phase 4.0: Attaches a LoRA dispatcher for zero-stall swapping.
     */
    void setLoraDispatcher(std::shared_ptr<Ronin::Kernel::Model::LoraDispatcher> ld) {
        m_lora_dispatcher = ld;
    }

    // Phase 4.1: Hardware Reality tracking
    double m_last_lat = 0.0;
    double m_last_lon = 0.0;

private:
    std::vector<Ronin::Kernel::CapabilityEntry> m_capabilities;
    std::unique_ptr<Model::InferenceEngine> m_inference_engine;
    std::shared_ptr<Ronin::Kernel::Checkpoint::CheckpointManager> m_checkpoint_manager;
    std::shared_ptr<Ronin::Kernel::Model::LoraDispatcher> m_lora_dispatcher;

    // Phase 4.0: Vtable-based Skill Registry
    std::unordered_map<uint32_t, std::shared_ptr<Ronin::Kernel::Capability::BaseSkill>> m_skill_registry;

    // Minimalist tokenizer
    std::vector<std::string> tokenize(const std::string& input);

    // Simple fuzzy match for typos (e.g., 'flashlite' vs 'flashlight')
    bool isFuzzyMatch(std::string_view word, std::string_view target);
};

} // namespace Ronin::Kernel::Intent
