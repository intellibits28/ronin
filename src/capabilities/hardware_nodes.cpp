#include "capabilities/hardware_nodes.h"
#include "capabilities/hardware_bridge.h"
#include "ronin_log.h"

namespace Ronin::Kernel::Capability {

static bool isActionOff(const std::string& param) {
    return (param.find("off") != std::string::npos || 
            param.find("stop") != std::string::npos || 
            param.find("disable") != std::string::npos ||
            param.find("ပိတ်") != std::string::npos);
}

std::string FlashlightNode::execute(const std::string& param) {
    bool intent_param = !isActionOff(param);
    bool success = HardwareBridge::triggerSync(getId(), intent_param);
    if (!success) return "Error: Flashlight trigger failed.";
    return intent_param ? "Success: Action Initiated - Flashlight ON" : "Success: Action Initiated - Flashlight OFF";
}

std::string LocationNode::execute(const std::string& param) {
    return HardwareBridge::requestData(getId());
}

std::string WifiNode::execute(const std::string& param) {
    bool intent_param = !isActionOff(param);
    bool success = HardwareBridge::triggerSync(getId(), intent_param);
    if (!success) return "Error: WiFi trigger failed.";
    return intent_param ? "Success: Action Initiated - WiFi ENABLE" : "Success: Action Initiated - WiFi DISABLE";
}

std::string BluetoothNode::execute(const std::string& param) {
    bool intent_param = !isActionOff(param);
    bool success = HardwareBridge::triggerSync(getId(), intent_param);
    if (!success) return "Error: Bluetooth trigger failed.";
    return intent_param ? "Success: Action Initiated - Bluetooth ENABLE" : "Success: Action Initiated - Bluetooth DISABLE";
}


} // namespace Ronin::Kernel::Capability
