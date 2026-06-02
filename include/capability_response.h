#pragma once

#include <string>

namespace Ronin::Kernel {

/**
 * v7.0: Encapsulates the driver's response back to the kernel.
 */
struct CapabilityResponse {
    std::string request_id;
    bool success;
    std::string payload_json; // Result data (e.g., lat/lon)
    std::string error;
};

} // namespace Ronin::Kernel
