#pragma once

#include <string>
#include <vector>
#include <memory>
#include "capabilities/tool_registry.h"

namespace Ronin::Kernel::Reasoning {

class CapabilityDiscoveryEngine {
public:
    CapabilityDiscoveryEngine();
    ~CapabilityDiscoveryEngine() = default;
    
    // Ranks tools based on capability requirements/queries
    std::vector<Capability::ToolMetadata> resolveCapabilities(const std::vector<std::string>& requirements);
    
    // Builds a directed acyclic execution graph (DAG) based on input/output compatibility
    std::vector<std::string> buildExecutionGraph(
        const std::vector<Capability::ToolMetadata>& resolved_tools,
        const std::vector<std::string>& initial_inputs
    );
};

} // namespace Ronin::Kernel::Reasoning
