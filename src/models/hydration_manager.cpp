#include "models/hydration_manager.h"
#include <fstream>
#include <sstream>
#include <cstdint>
#include <sys/stat.h>
#include <errno.h>

#define TAG "RoninHydration"

namespace Ronin::Kernel::Model {

HydrationManager::HydrationManager() : m_model_ptr((void*)-1), m_model_size(0), m_fd(-1), m_is_locked(false) {}

HydrationManager::~HydrationManager() {
    dehydrate();
}

uint64_t HydrationManager::getAvailableRAM() {
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        // Fallback for CI environments without /proc/meminfo
        return 2048ULL * 1024ULL * 1024ULL; // Assume 2GB
    }

    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.find("MemAvailable:") == 0) {
            uint64_t kb;
            std::stringstream ss(line.substr(13));
            ss >> kb;
            return kb * 1024ULL;
        }
    }
    return 1024ULL * 1024ULL * 1024ULL; // Fallback 1GB
}

bool HydrationManager::hydrate(const std::string& model_path) {
    dehydrate();

    m_fd = open(model_path.c_str(), O_RDONLY);
    if (m_fd == -1) {
        LOGE(TAG, "Hydration Error: Cannot open file %s (errno: %d)", model_path.c_str(), errno);
        return false;
    }

    struct stat st;
    if (fstat(m_fd, &st) == -1) {
        close(m_fd);
        m_fd = -1;
        return false;
    }
    m_model_size = st.st_size;

    // Phase 2: mmap with MAP_PRIVATE (Optimized for Mobile/Demand Paging)
    m_model_ptr = mmap(0, m_model_size, PROT_READ, MAP_PRIVATE, m_fd, 0);
    if (m_model_ptr == (void*)-1) {
        LOGE(TAG, "Hydration Error: mmap failed (errno: %d)", errno);
        close(m_fd);
        m_fd = -1;
        return false;
    }

    // Phase 2 Logic: Threshold Guard (1GB)
    uint64_t available = getAvailableRAM();
    LOGI(TAG, "Hydration: Available RAM is %llu MB", (unsigned long long)(available / (1024 * 1024)));

    if (available > 1024ULL * 1024ULL * 1024ULL) {
        LOGI(TAG, "Hydration: Attempting mlock for ultra-low latency.");
        if (mlock(m_model_ptr, m_model_size) == 0) {
            m_is_locked = true;
            LOGI(TAG, "Hydration: Model locked in physical RAM.");
        } else {
            LOGW(TAG, "Hydration: mlock failed (errno: %d). This is expected in some CI/Sandbox environments.", errno);
            // Continue without locking - disk paging will handle it.
        }
    } else {
        LOGW(TAG, "Hydration: Low RAM detected. Using demand-paging (mmap only) to ensure stability.");
    }

    // Advise kernel to expect random access patterns (LLM weights)
    madvise(m_model_ptr, m_model_size, MADV_RANDOM);

    return true;
}

void HydrationManager::dehydrate() {
    if (m_model_ptr != (void*)-1) {
        if (m_is_locked) {
            munlock(m_model_ptr, m_model_size);
            m_is_locked = false;
        }
        munmap(m_model_ptr, m_model_size);
        m_model_ptr = (void*)-1;
    }
    if (m_fd != -1) {
        close(m_fd);
        m_fd = -1;
    }
    m_model_size = 0;
}

bool HydrationManager::lockRegion(void* ptr, size_t size) {
    if (mlock(ptr, size) == 0) return true;
    return false;
}

void HydrationManager::unlockRegion(void* ptr, size_t size) {
    munlock(ptr, size);
}

} // namespace Ronin::Kernel::Model
