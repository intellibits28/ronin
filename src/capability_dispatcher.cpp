#include "capability_dispatcher.h"
#include "android_bridge.h"
#include "ronin_log.h"
#include "capabilities/tool_registry.h"
#include <nlohmann/json.hpp>

#define TAG "RoninDispatcher"

namespace Ronin::Kernel {

CapabilityDispatcher& CapabilityDispatcher::getInstance() {
    static CapabilityDispatcher instance;
    return instance;
}

void CapabilityDispatcher::dispatch(const CapabilityRequest& req, ResponseCallback callback) {
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        LOGI(TAG, "Dispatching capability request: %s (Type: %d)", req.request_id.c_str(), static_cast<int>(req.capability));
        if (callback) {
            m_pending_requests[req.request_id] = callback;
        }
    }

    // Check if the requested tool exists in the local C++ ToolRegistry first
    std::string action = "";
    try {
        auto j = nlohmann::json::parse(req.payload_json);
        if (j.contains("action")) {
            action = j["action"].get<std::string>();
        }
    } catch (...) {}

    if (!action.empty()) {
        auto& reg = Capability::ToolRegistry::getInstance();
        std::string res_val = reg.execute(action, req.payload_json);
        // If the tool is found in registry, complete it locally
        if (!res_val.starts_with("Error: Tool ") || res_val.find("not found.") == std::string::npos) {
            CapabilityResponse response;
            response.request_id = req.request_id;
            response.success = !res_val.starts_with("Error");
            response.payload_json = res_val;
            onResponse(response);
            return;
        }
    }

    // Layer 3 Integration: Hand off to AndroidBridge (OUTSIDE lock to prevent deadlocks)
    AndroidBridge::sendRequest(req);
}

void CapabilityDispatcher::onResponse(const CapabilityResponse& res) {
    ResponseCallback callback;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        
        LOGI(TAG, "Received capability response: %s (Success: %d)", res.request_id.c_str(), res.success);
        
        auto it = m_pending_requests.find(res.request_id);
        if (it != m_pending_requests.end()) {
            callback = std::move(it->second);
            m_pending_requests.erase(it);
        } else {
            LOGW(TAG, "No pending callback found for request_id: %s", res.request_id.c_str());
        }
    }

    if (callback) {
        callback(res);
    }
}

} // namespace Ronin::Kernel
