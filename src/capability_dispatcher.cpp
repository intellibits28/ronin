#include "capability_dispatcher.h"
#include "android_bridge.h"
#include "ronin_log.h"

#define TAG "RoninDispatcher"

namespace Ronin::Kernel {

CapabilityDispatcher& CapabilityDispatcher::getInstance() {
    static CapabilityDispatcher instance;
    return instance;
}

void CapabilityDispatcher::dispatch(const CapabilityRequest& req, ResponseCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    LOGI(TAG, "Dispatching capability request: %s (Type: %d)", req.request_id.c_str(), static_cast<int>(req.capability));
    
    if (callback) {
        m_pending_requests[req.request_id] = callback;
    }

    // Layer 3 Integration: Hand off to AndroidBridge
    AndroidBridge::sendRequest(req);
}

void CapabilityDispatcher::onResponse(const CapabilityResponse& res) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    LOGI(TAG, "Received capability response: %s (Success: %d)", res.request_id.c_str(), res.success);
    
    auto it = m_pending_requests.find(res.request_id);
    if (it != m_pending_requests.end()) {
        if (it->second) {
            it->second(res);
        }
        m_pending_requests.erase(it);
    } else {
        LOGW(TAG, "No pending callback found for request_id: %s", res.request_id.c_str());
    }
}

} // namespace Ronin::Kernel
