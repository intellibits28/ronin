#include "capabilities/hardware_nodes.h"
#include "capabilities/hardware_bridge.h"
#include "ronin_log.h"

namespace Ronin::Kernel::Capability {

static bool isActionOff(const std::string& param) {
    return (param.find("off") != std::string::npos || 
            param.find("stop") != std::string::npos || 
            param.find("disable") != std::string::npos);
}

std::string FlashlightNode::execute(const std::string& param) {
    bool isOff = isActionOff(param);
    HardwareBridge::triggerAsync(getId(), !isOff);
    return std::string("Success: Action Initiated - Flashlight ") + (isOff ? "OFF" : "ON");
}

std::string LocationNode::execute(const std::string& param) {
    return HardwareBridge::requestData(getId());
}

std::string WifiNode::execute(const std::string& param) {
    bool isOff = isActionOff(param);
    HardwareBridge::triggerAsync(getId(), !isOff);
    return std::string("Success: Action Initiated - WiFi ") + (isOff ? "DISABLE" : "ENABLE") + " (Opening Settings Panel)";
}

std::string BluetoothNode::execute(const std::string& param) {
    bool isOff = isActionOff(param);
    HardwareBridge::triggerAsync(getId(), !isOff);
    return std::string("Success: Action Initiated - Bluetooth ") + (isOff ? "DISABLE" : "ENABLE") + " (Opening Settings Panel/Request)";
}


} // namespace Ronin::Kernel::Capability
