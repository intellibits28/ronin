#pragma once

#include "base_skill.h"
#include <string>

namespace Ronin::Kernel::Capability {

class FlashlightNode : public BaseSkill {
public:
    uint32_t getId() const { return 4; }
    std::string getName() const override { return "FlashlightNode"; }
    uint32_t getLoraId() const override { return 3; }
    std::string execute(const std::string& param) override;
};

class LocationNode : public BaseSkill {
public:
    uint32_t getId() const { return 5; }
    std::string getName() const override { return "LocationNode"; }
    uint32_t getLoraId() const override { return 3; }
    std::string execute(const std::string& param) override;
};

class WifiNode : public BaseSkill {
public:
    uint32_t getId() const { return 6; }
    std::string getName() const override { return "WifiNode"; }
    uint32_t getLoraId() const override { return 3; }
    std::string execute(const std::string& param) override;
};

class BluetoothNode : public BaseSkill {
public:
    uint32_t getId() const { return 7; }
    std::string getName() const override { return "BluetoothNode"; }
    uint32_t getLoraId() const override { return 3; }
    std::string execute(const std::string& param) override;
};

} // namespace Ronin::Kernel::Capability
