#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include "base_skill.h"

namespace Ronin::Kernel::Capability {

struct ToolMetadata {
    std::string name;
    std::string description;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> required_permissions;
    float average_latency_ms = 0.0f;
    float success_rate = 1.0f;
};

class ToolRegistry {
public:
    static ToolRegistry& getInstance();
    
    // Registers a BaseSkill with metadata
    void registerSkill(std::shared_ptr<BaseSkill> skill, const ToolMetadata& meta);
    
    // Registers a functional-based tool (like DSP tools)
    void registerTool(const ToolMetadata& meta, std::function<std::string(const std::string&, ToolContext*)> impl);
    
    // Searches tools based on name or description matching
    std::vector<ToolMetadata> searchTools(const std::string& query);
    
    // Retrieves a registered skill by name
    std::shared_ptr<BaseSkill> getSkill(const std::string& name);
    
    // Executes a tool by name
    std::string execute(const std::string& name, const std::string& param, ToolContext* context = nullptr);
    
    // Clears all registered tools (for teardown/reinit)
    void clear();

private:
    ToolRegistry() = default;
    ~ToolRegistry() = default;
    
    struct RegisteredTool {
        ToolMetadata metadata;
        std::shared_ptr<BaseSkill> skill;
        std::function<std::string(const std::string&, ToolContext*)> func_impl;
    };
    
    std::unordered_map<std::string, RegisteredTool> m_tools;
};

} // namespace Ronin::Kernel::Capability
