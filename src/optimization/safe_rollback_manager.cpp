#include "optimization/safe_rollback_manager.h"
#include "shadow_testing/shadow_testing_manager.h"

namespace Ronin::Kernel::Optimization {

SafeRollbackManager::SafeRollbackManager(ShadowTestingManager* shadow_mgr)
    : m_shadow_mgr(shadow_mgr) {}

void SafeRollbackManager::beginTrial() {
    if (!m_snapshot_taken && m_shadow_mgr) {
        m_shadow_mgr->snapshot();
        m_snapshot_taken = true;
    }
}

void SafeRollbackManager::commit() {
    if (m_snapshot_taken && m_shadow_mgr) {
        // Discard the snapshot – no explicit API, just reset flag.
        m_snapshot_taken = false;
    }
}

void SafeRollbackManager::rollbackIfFailed(bool success) {
    if (!success && m_snapshot_taken && m_shadow_mgr) {
        m_shadow_mgr->restore();
        m_snapshot_taken = false;
    }
}

} // namespace Ronin::Kernel::Optimization
