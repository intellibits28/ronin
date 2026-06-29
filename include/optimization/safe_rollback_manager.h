#pragma once

#include <memory>

namespace Ronin::Kernel::Optimization {

class ShadowTestingManager;

/**
 * Simple safe‑rollback manager.
 * It leverages ShadowTestingManager to capture a clean state before attempting a
 * candidate capability execution. If the execution fails, the system can revert
 * to the previously captured snapshot.
 */
class SafeRollbackManager {
public:
    explicit SafeRollbackManager(ShadowTestingManager* shadow_mgr);

    // Capture the current state before a trial run.
    void beginTrial();

    // Commit the trial – discard the snapshot.
    void commit();

    // Roll back to the snapshot if `success` is false.
    void rollbackIfFailed(bool success);

private:
    ShadowTestingManager* m_shadow_mgr; // Not owned – owned by KernelRuntimeContext.
    bool m_snapshot_taken = false;
};

} // namespace Ronin::Kernel::Optimization
