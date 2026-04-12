#include "checkpoint_engine.h"
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <android/log.h>

#define TAG "RoninSurvivalCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace Ronin::Kernel::Checkpoint {

CheckpointEngine::CheckpointEngine(const std::string& checkpoint_path)
    : m_checkpoint_path(checkpoint_path) {}

CheckpointEngine::~CheckpointEngine() {
    if (m_memfd != -1) {
        close(m_memfd);
    }
}

/**
 * Creates a RAM-based, non-persistent shadow buffer using memfd_create.
 * memfd doesn't touch the disk, keeping Anchor 1 and 3 updates extremely fast.
 */
bool CheckpointEngine::initializeShadowBuffer(size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Create the anonymous, RAM-backed file descriptor
    m_memfd = memfd_create("ronin_shadow", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (m_memfd == -1) {
        LOGE("Failed to create memfd shadow buffer.");
        return false;
    }

    if (ftruncate(m_memfd, size) == -1) {
        LOGE("Failed to truncate memfd to %zu bytes.", size);
        return false;
    }

    m_buffer_size = size;
    LOGI("Survival Core shadow buffer initialized: %zu bytes", m_buffer_size);
    return true;
}

/**
 * Staged Checkpoint Logic:
 * Every time an edge is traversed, this updates the RAM-based shadow buffer.
 * Anchor 1 and 3 are always kept current in this RAM buffer.
 */
bool CheckpointEngine::updateCheckpointData(const uint8_t* data, size_t size) {
    if (m_memfd == -1 || size > m_buffer_size) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    // pwrite ensures we don't need to lseek, keeping it thread-safe.
    if (pwrite(m_memfd, data, size, 0) != static_cast<ssize_t>(size)) {
        LOGE("Failed to update staged checkpoint in memfd.");
        return false;
    }
    return true;
}

/**
 * Flushes the RAM shadow buffer to the physical storage path (/data/data/com.ronin.kernel/files/checkpoint.bin).
 * Uses fdatasync() to ensure durability on Android storage.
 */
bool CheckpointEngine::persistToStorage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_memfd == -1) return false;

    // Use a temporary file for atomic rename
    std::string tmp_path = m_checkpoint_path + ".tmp";
    int out_fd = open(tmp_path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (out_fd == -1) {
        LOGE("Failed to open storage path: %s", tmp_path.c_str());
        return false;
    }

    // Kernel-side copy: efficient data transfer from RAM to storage
    char buffer[4096];
    ssize_t n_read;
    lseek(m_memfd, 0, SEEK_SET);
    while ((n_read = read(m_memfd, buffer, sizeof(buffer))) > 0) {
        if (write(out_fd, buffer, n_read) != n_read) {
            LOGE("Failed to write to internal storage.");
            close(out_fd);
            return false;
        }
    }

    // Durability check
    if (fdatasync(out_fd) == -1) {
        LOGE("fdatasync failed for checkpoint.");
        close(out_fd);
        return false;
    }

    close(out_fd);

    // Atomic replacement of the valid checkpoint
    if (rename(tmp_path.c_str(), m_checkpoint_path.c_str()) == -1) {
        LOGE("Atomic rename to final checkpoint failed.");
        return false;
    }

    LOGI("Staged checkpoint successfully persisted to storage: %s", m_checkpoint_path.c_str());
    return true;
}

/**
 * Immediate flush upon receiving the LMK (Low Memory Killer) signal.
 */
void CheckpointEngine::onLMKSignal() {
    LOGI("LMK Pressure Detected: Flashing RAM shadow buffer to disk...");
    if (!persistToStorage()) {
        LOGE("Critical Failure: LMK-triggered flush failed.");
    }
}

} // namespace Ronin::Kernel::Checkpoint
