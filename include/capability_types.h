#pragma once

namespace Ronin::Kernel {

/**
 * v7.0: Fundamental system capabilities supported by the Ronin Agent Runtime.
 */
enum class CapabilityType {
    NONE = 0,
    LOCATION = 1,
    SMS = 2,
    SENSOR = 3,
    CAMERA = 4,
    AUDIO = 5,
    FILES = 6,
    MEMORY = 7,
    MAP = 8,
    TEST = 9,
    CONTACTS = 10
};

} // namespace Ronin::Kernel
