#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Ronin::Kernel {

/**
 * CapabilityCompiler gathers tool metadata and produces a compiled JSON manifest.
 * In a full system this would generate native code, but here we expose a manifest for A/B testing.
 */
class CapabilityCompiler {
public:
    CapabilityCompiler() = default;
    // Scan the ToolRegistry and emit a JSON description of capabilities
    std::string compileManifest() const;
};

} // namespace Ronin::Kernel
