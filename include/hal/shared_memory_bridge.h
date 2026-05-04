#pragma once

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "ronin_log.h"

namespace Ronin::Kernel::HAL {

/**
 * Phase 4.5: Shared Memory (SHM) System
 * Provides low-latency data access for high-frequency sensor data.
 */
template<typename T>
class SharedMemoryBridge {
public:
    SharedMemoryBridge(const std::string& name, size_t size) 
        : m_name(name), m_size(size * sizeof(T)) {
    }

    ~SharedMemoryBridge() {
        if (m_ptr != MAP_FAILED) {
            munmap(m_ptr, m_size);
        }
        if (m_fd != -1) {
            close(m_fd);
        }
    }

    bool create() {
#ifdef __ANDROID__
        // Android-specific SHM using ASharedMemory if available, 
        // but for now we'll use a portable mmap approach on a files-dir backed file.
        // Real ASharedMemory requires API 26+
        return false; // To be implemented with ASharedMemory_create
#else
        m_fd = shm_open(m_name.c_str(), O_CREAT | O_RDWR, 0666);
        if (m_fd == -1) return false;
        ftruncate(m_fd, m_size);
        m_ptr = mmap(0, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        return m_ptr != MAP_FAILED;
#endif
    }

    T* get() { return static_cast<T*>(m_ptr); }

private:
    std::string m_name;
    size_t m_size;
    int m_fd = -1;
    void* m_ptr = MAP_FAILED;
};

} // namespace Ronin::Kernel::HAL
