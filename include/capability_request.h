#pragma once

#include <string>
#include "capability_types.h"
#include "execution_context.h"

namespace Ronin::Kernel {

/**
 * v7.0: Encapsulates an agentic request to be dispatched to drivers.
 */
struct CapabilityRequest {
    std::string request_id;
    CapabilityType capability;
    std::string session_id;
    std::string payload_json; // JSON parameters for the specific tool
    Execution::ExecutionContextPtr exec_context; // v1.4 Execution Governance
};

} // namespace Ronin::Kernel
