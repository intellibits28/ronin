#include "capabilities/skill_compiler.h"
#include "capabilities/tool_registry.h"
#include <unordered_map>
#include <vector>
#include <sstream>
#include <nlohmann/json.hpp>
#include "ronin_log.h"

#define TAG "RoninSkillCompiler"

namespace Ronin::Kernel::Reasoning {

void SkillCompiler::compileAndPromoteSkills(Memory::LongTermMemory* ltm, int threshold) {
    if (!ltm || !ltm->getDatabase()) return;
    
    std::unordered_map<std::string, int> pattern_counts;
    std::unordered_map<std::string, std::vector<std::string>> serialized_to_steps;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT payload_json FROM episodes WHERE outcome_enum = 1;";
    
    if (sqlite3_prepare_v2(ltm->getDatabase(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* raw_payload = sqlite3_column_text(stmt, 0);
            if (!raw_payload) continue;
            
            try {
                nlohmann::json jPayload = nlohmann::json::parse(reinterpret_cast<const char*>(raw_payload));
                if (jPayload.contains("executed_steps") && jPayload["executed_steps"].is_array()) {
                    std::vector<std::string> steps = jPayload["executed_steps"].get<std::vector<std::string>>();
                    if (steps.size() > 1) {
                        // Serialize steps list to comma-separated key
                        std::stringstream ss;
                        for (size_t i = 0; i < steps.size(); ++i) {
                            ss << steps[i];
                            if (i < steps.size() - 1) ss << ",";
                        }
                        std::string key = ss.str();
                        pattern_counts[key]++;
                        serialized_to_steps[key] = steps;
                    }
                }
            } catch (...) {
                // Ignore parsing errors for mock / corrupt rows
            }
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    
    auto& registry = Capability::ToolRegistry::getInstance();
    
    // Evaluate pattern counts and compile macro-skills
    for (const auto& [pattern_str, count] : pattern_counts) {
        if (count >= threshold) {
            const auto& steps = serialized_to_steps[pattern_str];
            
            // Construct a unique clean name for the macro-skill
            std::stringstream name_ss;
            name_ss << "macro_skill";
            for (const auto& step : steps) {
                name_ss << "_" << step;
            }
            std::string macro_name = name_ss.str();
            
            // Verify if it's already registered to prevent duplicate warning outputs
            auto existing = registry.searchTools(macro_name);
            if (existing.empty()) {
                Capability::ToolMetadata meta;
                meta.name = macro_name;
                meta.description = "Self-compiled Macro-Skill executing sequence: " + pattern_str;
                
                // Define inputs of macro as first step inputs, outputs as last step outputs
                auto first_tool = registry.searchTools(steps.front());
                if (!first_tool.empty()) meta.inputs = first_tool[0].inputs;
                
                auto last_tool = registry.searchTools(steps.back());
                if (!last_tool.empty()) meta.outputs = last_tool[0].outputs;
                
                // Register the dynamic sequencer tool
                registry.registerTool(meta, [steps](const std::string& initial_payload, ToolContext* ctx) -> std::string {
                    auto& reg = Capability::ToolRegistry::getInstance();
                    std::string current_payload = initial_payload;
                    for (const auto& step_name : steps) {
                        current_payload = reg.execute(step_name, current_payload, ctx);
                        if (current_payload.rfind("Error", 0) == 0) {
                            return "Error: Macro-Skill execution aborted at step: " + step_name + ". Details: " + current_payload;
                        }
                    }
                    return current_payload;
                });
                
                // Store note to LTM notes table for search and future bypasses
                ltm->storeNote("Discovered Macro-Skill", "Macro-Skill '" + macro_name + "' compiles sequence: " + pattern_str, "macro_skill");
                LOGI(TAG, "Successfully promoted recurrent sequence to Macro-Skill: %s (Count: %d)", macro_name.c_str(), count);
            }
        }
    }
}

} // namespace Ronin::Kernel::Reasoning
