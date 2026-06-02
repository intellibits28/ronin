#pragma once

#include <string>
#include "capability_types.h"

namespace Ronin::Kernel {

/**
 * v7.0: Encapsulates an agentic request to be dispatched to drivers.
 */
struct CapabilityRequest {
    std::string request_id;
    CapabilityType capability;
    std::string session_id;
    std::string payload_json; // JSON parameters for the specific tool
};

} // namespace Ronin::Kernel
