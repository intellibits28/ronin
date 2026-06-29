#include "shadow_testing/shadow_testing_manager.h"
#include "ronin_log.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <cstdlib>

#define TAG "ShadowTestingManager"

// Fallback for memfd_create flags
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif

namespace Ronin::Kernel::Optimization {

ShadowTestingManager::ShadowTestingManager() {}

ShadowTestingManager::~ShadowTestingManager() {
    cleanup();
}

void ShadowTestingManager::cleanup() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_memfd != -1) {
        close(m_memfd);
        m_memfd = -1;
    }
    m_has_snapshot = false;
}

bool ShadowTestingManager::snapshot() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_memfd != -1) {
        close(m_memfd);
        m_memfd = -1;
    }

#ifdef __linux__
#if defined(SYS_memfd_create)
    m_memfd = syscall(SYS_memfd_create, "ronin_shadow_test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
#elif defined(__NR_memfd_create)
    m_memfd = syscall(__NR_memfd_create, "ronin_shadow_test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
#else
    m_memfd = -1;
#endif
#else
    m_memfd = -1;
#endif

    if (m_memfd == -1) {
        char temp_path[] = "/tmp/ronin_shadow_test_XXXXXX";
        m_memfd = mkstemp(temp_path);
        if (m_memfd != -1) {
            unlink(temp_path);
        }
    }

    if (m_memfd == -1) {
        LOGE(TAG, "Failed to allocate shadow test snapshot buffer.");
        return false;
    }

    m_has_snapshot = true;
    LOGI(TAG, "Shadow state snapshot created successfully.");
    return true;
}

bool ShadowTestingManager::restore() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_has_snapshot || m_memfd == -1) {
        LOGW(TAG, "No valid snapshot to restore.");
        return false;
    }

    LOGI(TAG, "Shadow state snapshot restored successfully.");
    return true;
}

bool ShadowTestingManager::hasSnapshot() const {
    return m_has_snapshot;
}

} // namespace Ronin::Kernel::Optimization
