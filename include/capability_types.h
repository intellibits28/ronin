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
    CONTACTS = 10,
    ALARM = 11,
    CALENDAR = 12,
    MAIL = 13
};

inline const char* CapabilityTypeToString(CapabilityType type) {
    switch (type) {
        case CapabilityType::LOCATION: return "LOCATION";
        case CapabilityType::SMS: return "SMS";
        case CapabilityType::SENSOR: return "SENSOR";
        case CapabilityType::CAMERA: return "CAMERA";
        case CapabilityType::AUDIO: return "AUDIO";
        case CapabilityType::FILES: return "FILES";
        case CapabilityType::MEMORY: return "MEMORY";
        case CapabilityType::MAP: return "MAP";
        case CapabilityType::TEST: return "TEST";
        case CapabilityType::CONTACTS: return "CONTACTS";
        case CapabilityType::ALARM: return "ALARM";
        case CapabilityType::CALENDAR: return "CALENDAR";
        case CapabilityType::MAIL: return "MAIL";
        default: return "NONE";
    }
}

} // namespace Ronin::Kernel
