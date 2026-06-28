#include "capabilities/tool_registry.h"
#include <algorithm>
#include "ronin_log.h"

#define TAG "RoninToolRegistry"

namespace Ronin::Kernel::Capability {

ToolRegistry& ToolRegistry::getInstance() {
    static ToolRegistry instance;
    return instance;
}

void ToolRegistry::registerSkill(std::shared_ptr<BaseSkill> skill, const ToolMetadata& meta) {
    if (!skill) return;
    std::string name = meta.name.empty() ? skill->getName() : meta.name;
    
    // Convert to lowercase for uniform search mapping
    std::string name_lower = name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
    
    RegisteredTool tool;
    tool.metadata = meta;
    tool.metadata.name = name; // ensure uniform name
    tool.skill = skill;
    
    m_tools[name_lower] = tool;
    LOGI(TAG, "Registered Skill tool: %s (%s)", name.c_str(), meta.description.c_str());
}

void ToolRegistry::registerTool(const ToolMetadata& meta, std::function<std::string(const std::string&, ToolContext*)> impl) {
    if (!impl) return;
    
    std::string name_lower = meta.name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
    
    RegisteredTool tool;
    tool.metadata = meta;
    tool.func_impl = impl;
    
    m_tools[name_lower] = tool;
    LOGI(TAG, "Registered Functional tool: %s (%s)", meta.name.c_str(), meta.description.c_str());
}

std::vector<ToolMetadata> ToolRegistry::searchTools(const std::string& query) {
    std::vector<ToolMetadata> results;
    std::string q_lower = query;
    std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), ::tolower);
    
    for (const auto& [name, tool] : m_tools) {
        std::string name_check = tool.metadata.name;
        std::transform(name_check.begin(), name_check.end(), name_check.begin(), ::tolower);
        
        std::string desc_check = tool.metadata.description;
        std::transform(desc_check.begin(), desc_check.end(), desc_check.begin(), ::tolower);
        
        // Simple substring matching for keyword discovery
        if (name_check.find(q_lower) != std::string::npos || desc_check.find(q_lower) != std::string::npos) {
            results.push_back(tool.metadata);
        }
    }
    return results;
}

std::shared_ptr<BaseSkill> ToolRegistry::getSkill(const std::string& name) {
    std::string name_lower = name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
    
    auto it = m_tools.find(name_lower);
    if (it != m_tools.end()) {
        return it->second.skill;
    }
    return nullptr;
}

std::string ToolRegistry::execute(const std::string& name, const std::string& param, ToolContext* context) {
    std::string name_lower = name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
    
    auto it = m_tools.find(name_lower);
    if (it == m_tools.end()) {
        return "Error: Tool " + name + " not found.";
    }
    
    if (it->second.skill) {
        return it->second.skill->execute(param, context);
    } else if (it->second.func_impl) {
        return it->second.func_impl(param, context);
    }
    return "Error: Tool implementation not configured.";
}

void ToolRegistry::clear() {
    m_tools.clear();
    LOGI(TAG, "Cleared all registered tools.");
}

} // namespace Ronin::Kernel::Capability
