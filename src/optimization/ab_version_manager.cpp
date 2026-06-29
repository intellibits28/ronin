#include "optimization/ab_version_manager.h"
#include <sstream>

namespace Ronin::Kernel::Optimization {

std::string ABVersionManager::registerCandidate(const std::string& manifest) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream oss;
    oss << "candidate_" << m_next_id++;
    std::string id = oss.str();
    m_metrics[id] = Metrics{0, 0, std::chrono::steady_clock::now()};
    // Store as baseline if none yet
    if (m_baseline_manifest.empty()) {
        m_baseline_manifest = manifest;
    }
    return id;
}

void ABVersionManager::recordResult(const std::string& manifest_id, bool success) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_metrics.find(manifest_id);
    if (it == m_metrics.end()) return;
    if (success) {
        ++it->second.successes;
    } else {
        ++it->second.failures;
    }
}

bool ABVersionManager::shouldPromote(const std::string& manifest_id, size_t success_threshold) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_metrics.find(manifest_id);
    if (it == m_metrics.end()) return false;
    return it->second.successes >= success_threshold;
}

void ABVersionManager::promote(const std::string& manifest_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // In a real system we'd retrieve the manifest; here we just set baseline flag.
    // Assume caller will manage manifest storage.
    // For simplicity, we just clear metrics after promotion.
    auto it = m_metrics.find(manifest_id);
    if (it != m_metrics.end()) {
        // Promotion means this candidate becomes baseline.
        // Here we don't have the actual manifest string; assume caller passes it separately.
        // Clear metrics for other candidates.
        m_metrics.clear();
        m_next_id = 1;
    }
}

std::string ABVersionManager::getBaselineManifest() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_baseline_manifest;
}

} // namespace Ronin::Kernel::Optimization
