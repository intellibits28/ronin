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
#include "memory_manager.h"
#include "long_term_memory.h"
#include "myanmar_segmenter.h"
#include "capability_types.h"
#include <nlohmann/json.hpp>

namespace Ronin::Kernel::Intent {

/**
 * v7.0: Orchestrates complex multi-step tasks using LLM-driven planning.
 */
class TaskPlanner {
public:
    explicit TaskPlanner(Model::InferenceEngine* engine);
    
    // Generates a structured JSON plan from raw user input
    AgentPlan createPlan(const std::string& input);
    
    // Parses raw JSON string into AgentPlan struct
    bool parsePlan(const std::string& json_str, AgentPlan& out_plan);

    // v7.0 Layer 5: Maps intent strings to capability types
    CapabilityType mapIntentToCapability(const std::string& intent_name);

private:
    Model::InferenceEngine* m_engine;
};

enum class ThermalState {
    NORMAL,
    MODERATE,
    SEVERE
};

// Global thermal state (should be updated by Android PowerManager/Thermal HAL)
extern ThermalState g_thermal_state;

class IntentEngine {
public:
    IntentEngine(Memory::LongTermMemory* ltm = nullptr);

    /**
     * Loads capability manifest from a JSON-like formatted file.
     */
    void loadCapabilities(const std::string& json_path);

    /**
     * Attaches an ONNX inference engine for Tier 3 detection.
     */
    void setInferenceEngine(std::unique_ptr<Model::InferenceEngine> engine) {
        m_inference_engine = std::move(engine);
        m_planner = std::make_unique<TaskPlanner>(m_inference_engine.get());
        
        // v10.2.7: Update ChatSkill engine if it's already registered
        auto skill = getSkill(8); // Node 8 is ChatSkill
        if (skill) {
            auto chat = std::dynamic_pointer_cast<Ronin::Kernel::Capability::ChatSkill>(skill);
            if (chat) {
                // We need a setEngine method in ChatSkill or we can just re-register
                registerSkill(8, std::make_shared<Ronin::Kernel::Capability::ChatSkill>(m_inference_engine.get(), m_ltm));
            }
        }
    }

    /**
     * @return The attached inference engine.
     */
    Model::InferenceEngine* getInferenceEngine() const {
        return m_inference_engine.get();
    }

    TaskPlanner* getPlanner() const {
        return m_planner.get();
    }

    /**
     * Attaches a memory manager for system status commands.
     */
    void setMemoryManager(Memory::MemoryManager* mm) {
        m_memory_manager = mm;
    }

    /**
     * Processes raw input to determine the high-level intent score.
     */
    CognitiveIntent process(const std::string& input, const std::string& context_subject = "");

    /**
     * Tier 0: Command Interface
     * Intercepts and executes system commands starting with '/'.
     * @param input The raw user input.
     * @param output The response string to display in the UI console.
     * @return True if a command was handled.
     */
    bool handleCommand(const std::string& input, std::string& output);

    /**
     * Phase 4.0: Vtable-based Skill Execution
     * @param nodeId The target node ID from the Reasoning Spine.
     * @param param The extracted parameter for this tool.
     * @param context Optional shared context for tool chaining.
     * @return A response string for the UI.
     */
    std::string executeSkill(uint32_t nodeId, const std::string& param, ToolContext* context = nullptr);

    /**
     * Checks if a modular skill is registered for the given ID.
     */
    bool hasSkill(uint32_t nodeId) const {
        return m_skill_registry.find(nodeId) != m_skill_registry.end();
    }

    /**
     * @return The registered skill for the given ID, or nullptr if not found.
     */
    std::shared_ptr<Ronin::Kernel::Capability::BaseSkill> getSkill(uint32_t id) const {
        auto it = m_skill_registry.find(id);
        return (it != m_skill_registry.end()) ? it->second : nullptr;
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
     * Phase 4.4: Attaches a LoRA dispatcher for zero-stall swapping.
     */
    void setLoraDispatcher(std::shared_ptr<Ronin::Kernel::Model::LoraDispatcher> ld) {
        m_lora_dispatcher = ld;
    }

    /**
     * Phase 4.6.3: Multi-Provider Logic Marriage
     * Toggles whether cloud escalation is allowed.
     */
    void setOfflineMode(bool offline) {
        m_offline_mode = offline;
    }

    bool isOfflineMode() const {
        return m_offline_mode;
    }

    void setPrimaryCloudProvider(const std::string& provider) {
        m_primary_cloud_provider = provider;
    }

    std::string getPrimaryCloudProvider() const {
        return m_primary_cloud_provider;
    }

    /**
     * Phase 5.0: Metadata Awareness
     * Updates internal registry with live model specs (thinking, token limits).
     */
    bool updateMetadata(const std::string& json_metadata);

    void updateLocation(double lat, double lon) {
        m_last_lat = lat;
        m_last_lon = lon;
    }

    // Phase 4.1: Hardware Reality tracking
    double m_last_lat = 0.0;
    double m_last_lon = 0.0;

    /**
     * Phase 6.6: Task Management
     * Stops all LOW_PRIORITY skills (e.g. background indexing).
     */
    void stopLowPriorityTasks();

    /**
     * Sets the global execution priority.
     */
    void setPriority(Ronin::Kernel::Capability::SkillPriority priority) {
        m_current_priority = priority;
    }

    /**
     * Sets the callback to be invoked when low-priority tasks must stop.
     */
    void setLowPriorityStopCallback(std::function<void()> callback) {
        m_stop_callback = callback;
    }

    /**
     * Phase 9.0: Proactive resource management.
     * Propagates memory pressure signals to all registered skills.
     */
    void notifyTrimMemory(int level);

    /**
     * Phase 3: Tool Calling Guard-rails
     */
    static constexpr int MAX_TOOL_CALL_DEPTH = 1;
    void resetToolDepth() { m_current_tool_depth = 0; }

    /**
     * Phase 4: Gemma 4 Tool Dispatcher
     * Parses the 'CALL: tool_name(\"args\")' pattern and executes it.
     * @param llm_output The raw output from Gemma 4.
     * @return The execution result or original output if no tool was called.
     */
    std::string dispatchToolCall(const std::string& llm_output);

private:
    Memory::LongTermMemory* m_ltm = nullptr;
    std::function<void()> m_stop_callback;
    std::vector<Ronin::Kernel::CapabilityEntry> m_capabilities;
    std::unique_ptr<Model::InferenceEngine> m_inference_engine;
    Memory::MemoryManager* m_memory_manager = nullptr;
    std::shared_ptr<Ronin::Kernel::Checkpoint::CheckpointManager> m_checkpoint_manager;
    std::shared_ptr<Ronin::Kernel::Model::LoraDispatcher> m_lora_dispatcher;
    bool m_offline_mode = false;
    std::string m_primary_cloud_provider = "Gemini";
    Ronin::Kernel::Capability::SkillPriority m_current_priority = Ronin::Kernel::Capability::SkillPriority::LOW;
    std::string m_last_command_output;
    int m_current_tool_depth = 0;
    std::unique_ptr<TaskPlanner> m_planner;

    // Phase 4.0: Vtable-based Skill Registry
    std::unordered_map<uint32_t, std::shared_ptr<Ronin::Kernel::Capability::BaseSkill>> m_skill_registry;

    // Minimalist tokenizer
    std::vector<std::string> tokenize(const std::string& input);

    // Simple fuzzy match for typos (e.g., 'flashlite' vs 'flashlight')
    bool isFuzzyMatch(std::string_view word, std::string_view target);

    // Phase 5.0: Model Registry Metadata
    struct ModelMetadata {
        std::string name;
        std::string displayName;
        bool thinking = false;
        int inputTokenLimit = 2048;
    };
    std::unordered_map<std::string, ModelMetadata> m_model_metadata;
};

} // namespace Ronin::Kernel::Intent
