#pragma once

#include "capability_types.h"
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace Ronin::Kernel {

/**
 * v7.0 Layer 9: Manages exclusive access to hardware resources.
 */
class ResourceManager {
public:
    static ResourceManager& getInstance();

    // Requests exclusive lock on a resource
    bool acquireResource(CapabilityType resource, const std::string& session_id);

    // Releases a resource lock
    void releaseResource(CapabilityType resource, const std::string& session_id);

    // Checks if a resource is available
    bool isAvailable(CapabilityType resource) const;

private:
    ResourceManager() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<CapabilityType, std::string> m_locks; // Resource -> session_id
};

} // namespace Ronin::Kernel
