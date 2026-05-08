#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "ronin_log.h"

namespace Ronin::Kernel::Model {

/**
 * Phase 2: HydrationManager
 * Handles mmap/mlock logic with RAM pressure guards and portability.
 */
class HydrationManager {
public:
    HydrationManager();
    ~HydrationManager();

    /**
     * Hydrates a model using mmap.
     * Implements a 1GB Threshold Guard for mlock.
     */
    bool hydrate(const std::string& model_path);
    
    /**
     * Releases memory mappings.
     */
    void dehydrate();

    void* getModelPtr() const { return m_model_ptr; }
    size_t getModelSize() const { return m_model_size; }
    bool isLocked() const { return m_is_locked; }

    /**
     * Dynamic Region Locking (e.g., for KV Cache during inference).
     */
    bool lockRegion(void* ptr, size_t size);
    void unlockRegion(void* ptr, size_t size);

    /**
     * Reads /proc/meminfo to check available RAM.
     */
    static uint64_t getAvailableRAM();

private:
    void* m_model_ptr = (void*)-1; // MAP_FAILED
    size_t m_model_size = 0;
    int m_fd = -1;
    bool m_is_locked = false;
};

} // namespace Ronin::Kernel::Model
