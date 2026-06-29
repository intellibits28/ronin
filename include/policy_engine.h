#pragma once

#include <string>
#include <unordered_map>
#include <optional>

namespace Ronin::Kernel {

/**
 * Simple PolicyEngine that checks capability ids against a hard‑coded policy table.
 * Returns true if the capability is allowed, false otherwise.
 */
class PolicyEngine {
public:
    PolicyEngine();
    // Evaluate a capability id; optional reason on denial.
    bool evaluate(const std::string &capabilityId, std::optional<std::string> &reason) const;
private:
    struct Policy {
        bool allowed;
        std::string denialReason;
    };
    std::unordered_map<std::string, Policy> m_policies;
};

} // namespace Ronin::Kernel
