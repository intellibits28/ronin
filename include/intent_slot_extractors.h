#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace Ronin::Kernel::Intent {

struct SmsSlotData {
    std::string recipient;
    std::string message;
    bool attach_location = false;
    bool is_valid = false;
};

struct AlarmSlotData {
    std::string time_str; // e.g. "07:00", "19:30"
    std::string date_str; // "tomorrow", "today", etc.
    std::string label;
    bool is_valid = false;
};

struct VaultSlotData {
    std::string title;
    std::string content;
    bool is_save = false; // true = save, false = lookup
    bool is_valid = false;
};

struct FileSearchSlotData {
    std::string query;
    std::string file_extension;
    bool is_valid = false;
};

struct LocationSlotData {
    std::string entity;    // e.g. "Home", "Work", "Location"
    std::string destination;
    bool is_saving = false;
    bool is_map_open = false;
};

/**
 * Dedicated Slot & Entity Extractor module.
 * Separates parameter extraction from intent classification logic.
 */
class IntentSlotExtractors {
public:
    static SmsSlotData extractSmsSlots(const std::string& input);
    static AlarmSlotData extractAlarmSlots(const std::string& input);
    static VaultSlotData extractVaultSlots(const std::string& input);
    static FileSearchSlotData extractFileSearchSlots(const std::string& input);
    static LocationSlotData extractLocationSlots(const std::string& input);

private:
    static std::string trim(const std::string& str);
};

} // namespace Ronin::Kernel::Intent
