#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace Ronin::Kernel::Optimization {

/**
 * Lightweight A/B version promotion manager.
 * Tracks success/failure metrics for candidate capability manifests and promotes
 * a candidate to the baseline when it meets a configurable success threshold.
 */
class ABVersionManager {
public:
    struct Metrics {
        size_t successes = 0;
        size_t failures = 0;
        std::chrono::steady_clock::time_point first_seen = std::chrono::steady_clock::now();
    };

    ABVersionManager() = default;

    // Register a new candidate manifest (JSON string) and receive a unique ID.
    std::string registerCandidate(const std::string& manifest);

    // Record the outcome of an execution using the given manifest ID.
    void recordResult(const std::string& manifest_id, bool success);

    // Returns true if the candidate has reached the success threshold.
    bool shouldPromote(const std::string& manifest_id, size_t success_threshold = 10) const;

    // Promote the candidate manifest to become the baseline.
    void promote(const std::string& manifest_id);

    // Retrieve the current baseline manifest JSON.
    std::string getBaselineManifest() const;

private:
    mutable std::mutex m_mutex;
    std::string m_baseline_manifest;
    std::unordered_map<std::string, Metrics> m_metrics;
    size_t m_next_id = 1;
};

} // namespace Ronin::Kernel::Optimization
