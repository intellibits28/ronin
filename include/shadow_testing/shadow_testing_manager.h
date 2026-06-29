#pragma once

#include <string>
#include <vector>
#include <mutex>

namespace Ronin::Kernel::Optimization {

/**
 * ShadowTestingManager manages shadow execution environments and state snapshots
 * using memfd_create (or temporary file fallbacks) to allow safe trial runs
 * of new models/capabilities without impacting the live system state.
 */
class ShadowTestingManager {
public:
    ShadowTestingManager();
    ~ShadowTestingManager();

    // Snapshot current state into RAM memory buffer (memfd)
    bool snapshot();

    // Restore state from previously captured RAM buffer
    bool restore();

    // Check if a snapshot currently exists
    bool hasSnapshot() const;

private:
    int m_memfd = -1;
    bool m_has_snapshot = false;
    std::mutex m_mutex;

    void cleanup();
};

} // namespace Ronin::Kernel::Optimization
