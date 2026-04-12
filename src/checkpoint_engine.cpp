#include "checkpoint_engine.h"
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include "ronin_log.h"

#define TAG "RoninSurvivalCore"

namespace Ronin::Kernel::Checkpoint {

CheckpointEngine::CheckpointEngine(const std::string& checkpoint_path)
    : m_checkpoint_path(checkpoint_path) {}

CheckpointEngine::~CheckpointEngine() {
    if (m_memfd != -1) {
        close(m_memfd);
    }
}

bool CheckpointEngine::initializeShadowBuffer(size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_memfd != -1) {
        close(m_memfd);
    }

    m_memfd = memfd_create("ronin_shadow", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (m_memfd == -1) {
        LOGE(TAG, "Failed to create memfd shadow buffer.");
        return false;
    }

    if (ftruncate(m_memfd, size) == -1) {
        LOGE(TAG, "Failed to truncate memfd to %zu bytes.", size);
        return false;
    }

    m_buffer_size = size;
    LOGI(TAG, "Survival Core shadow buffer initialized: %zu bytes", m_buffer_size);
    return true;
}

bool CheckpointEngine::syncBufferToDisk() {
    // This is a helper for persistToStorage logic
    return persistToStorage();
}

bool CheckpointEngine::updateCheckpointData(const uint8_t* data, size_t size) {
    if (m_memfd == -1 || size > m_buffer_size) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (pwrite(m_memfd, data, size, 0) != static_cast<ssize_t>(size)) {
        LOGE(TAG, "Failed to update staged checkpoint in memfd.");
        return false;
    }
    return true;
}

bool CheckpointEngine::persistToStorage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_memfd == -1) return false;

    std::string tmp_path = m_checkpoint_path + ".tmp";
    int out_fd = open(tmp_path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (out_fd == -1) {
        LOGE(TAG, "Failed to open storage path: %s", tmp_path.c_str());
        return false;
    }

    char buffer[4096];
    ssize_t n_read;
    lseek(m_memfd, 0, SEEK_SET);
    bool write_success = true;
    while ((n_read = read(m_memfd, buffer, sizeof(buffer))) > 0) {
        if (write(out_fd, buffer, n_read) != n_read) {
            LOGE(TAG, "Failed to write to internal storage.");
            write_success = false;
            break;
        }
    }

    if (!write_success) {
        close(out_fd);
        unlink(tmp_path.c_str());
        return false;
    }

    if (fdatasync(out_fd) == -1) {
        LOGE(TAG, "fdatasync failed for checkpoint.");
        close(out_fd);
        return false;
    }

    close(out_fd);

    if (rename(tmp_path.c_str(), m_checkpoint_path.c_str()) == -1) {
        LOGE(TAG, "Atomic rename to final checkpoint failed.");
        return false;
    }

    LOGI(TAG, "Staged checkpoint successfully persisted to storage: %s", m_checkpoint_path.c_str());
    return true;
}

void CheckpointEngine::onLMKSignal() {
    LOGI(TAG, "LMK Pressure Detected: Flashing RAM shadow buffer to disk...");
    if (!persistToStorage()) {
        LOGE(TAG, "Critical Failure: LMK-triggered flush failed.");
    }
}

} // namespace Ronin::Kernel::Checkpoint
