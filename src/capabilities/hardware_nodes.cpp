#include "capabilities/hardware_nodes.h"
#include "ronin_log.h"

namespace Ronin::Kernel::Capability {

std::string FlashlightNode::execute(const std::string& param) {
    bool isOff = (param.find("off") != std::string::npos || param.find("stop") != std::string::npos || param.find("disable") != std::string::npos);
    return std::string("Success: Action Initiated - Flashlight ") + (isOff ? "OFF" : "ON");
}

std::string LocationNode::execute(const std::string& param) {
    return "Success: Action Initiated - Locating device...";
}

std::string WifiNode::execute(const std::string& param) {
    bool isOff = (param.find("off") != std::string::npos || param.find("stop") != std::string::npos || param.find("disable") != std::string::npos);
    return std::string("Success: Action Initiated - WiFi ") + (isOff ? "DISABLE" : "ENABLE") + " (Opening Settings Panel)";
}

std::string BluetoothNode::execute(const std::string& param) {
    bool isOff = (param.find("off") != std::string::npos || param.find("stop") != std::string::npos || param.find("disable") != std::string::npos);
    return std::string("Success: Action Initiated - Bluetooth ") + (isOff ? "DISABLE" : "ENABLE") + " (Opening Settings Panel/Request)";
}

} // namespace Ronin::Kernel::Capability
