#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Ronin::Kernel::HAL {

/**
 * Phase 4.5: Ronin Hardware Abstraction Layer (HAL)
 * Decouples physical hardware control from the reasoning spine.
 */
enum class DeviceType {
    WIFI,
    BLUETOOTH,
    GPS,
    FLASHLIGHT,
    ACCELEROMETER,
    GYROSCOPE,
    THERMAL
};

struct DeviceStatus {
    DeviceType type;
    bool enabled;
    std::string data;
};

class IRoninHAL {
public:
    virtual ~IRoninHAL() = default;

    /**
     * Initializes the hardware module.
     */
    virtual bool initialize() = 0;

    /**
     * Synchronously toggles a device state.
     */
    virtual bool setDeviceState(DeviceType type, bool state) = 0;

    /**
     * Retrieves the latest data from a device.
     */
    virtual DeviceStatus getDeviceStatus(DeviceType type) = 0;
};

} // namespace Ronin::Kernel::HAL
