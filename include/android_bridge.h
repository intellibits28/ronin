#pragma once

#include "capability_request.h"
#include <nlohmann/json.hpp>

namespace Ronin::Kernel {

/**
 * v7.0 Layer 3: JNI Message Bridge (C++ side).
 * Converts structured requests into JSON for the Kotlin driver layer.
 */
class AndroidBridge {
public:
    static void sendRequest(const CapabilityRequest& req);
};

} // namespace Ronin::Kernel
