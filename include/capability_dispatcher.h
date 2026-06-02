#pragma once

#include "capability_request.h"
#include "capability_response.h"
#include <functional>
#include <unordered_map>
#include <string>
#include <mutex>

namespace Ronin::Kernel {

/**
 * v7.0 Layer 2: The kernel's syscall router for agentic capabilities.
 */
class CapabilityDispatcher {
public:
    using ResponseCallback = std::function<void(const CapabilityResponse&)>;

    static CapabilityDispatcher& getInstance();

    // Dispatches a request to the appropriate driver/bridge
    void dispatch(const CapabilityRequest& req, ResponseCallback callback = nullptr);

    // Called by bridges when a response is received
    void onResponse(const CapabilityResponse& res);

private:
    CapabilityDispatcher() = default;
    
    std::mutex m_mutex;
    std::unordered_map<std::string, ResponseCallback> m_pending_requests;
};

} // namespace Ronin::Kernel
