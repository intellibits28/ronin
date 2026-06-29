#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <nlohmann/json.hpp>

namespace Ronin::Kernel {

/**
 * PolicyDecision enumerates the possible outcomes of a policy check.
 */
enum class PolicyDecision {
    ALLOW,
    DENY,
    DEFER // e.g., requires human‑in‑the‑loop confirmation
};

/**
 * ReasonCode provides a machine‑readable identifier for why a decision was made.
 */
enum class ReasonCode {
    NONE,
    BATTERY_LOW,
    OFFLINE,
    PRIVACY_BLOCKED,
    HITL_REQUIRED,
    VERSION_MISMATCH,
    UNKNOWN_CAPABILITY
};

/**
 * CapabilityPolicyEngine validates capability execution against a static policy manifest.
 *
 * The manifest is a JSON file located in the Android assets folder
 * ("assets/policy_manifest.json"). Example entry:
 *
 *   "audio_capture": {
 *       "allowed": true,
 *       "battery_min": 20,
 *       "requires_network": false,
 *       "requires_human": false,
 *       "allowed_versions": [1,2]
 *   }
 *
 * The engine loads this file at construction time and provides an evaluate()
 * method that returns a decision and an optional reason code.
 */
class CapabilityPolicyEngine {
public:
    CapabilityPolicyEngine();

    /**
     * Evaluate a capability against current device state.
     * @param capabilityId Identifier of the capability (e.g., "audio_capture").
     * @param batteryLevel Percentage (0‑100) of remaining battery.
     * @param isOnline true if device has network connectivity.
     * @param privacyAllowed true if user consent permits this capability.
     * @param version Currently selected capability version.
     * @param reason Optional output parameter populated with a ReasonCode when the
     *               decision is DENY or DEFER.
     * @return PolicyDecision indicating whether execution may proceed.
     */
    PolicyDecision evaluate(const std::string &capabilityId,
                            int batteryLevel,
                            bool isOnline,
                            bool privacyAllowed,
                            int version,
                            std::optional<ReasonCode> &reason) const;

private:
    struct PolicyEntry {
        bool allowed = true;
        int battery_min = 0;               // minimum battery percentage required
        bool requires_network = false;
        bool requires_human = false;
        std::vector<int> allowed_versions; // empty means any version
    };

    // Map from capability id -> policy entry
    std::unordered_map<std::string, PolicyEntry> m_policyMap;

    // Helper to load the JSON manifest from the assets directory.
    void loadManifest();
};

} // namespace Ronin::Kernel
