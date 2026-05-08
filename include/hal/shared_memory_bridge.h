#pragma once

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <atomic>
#include <cstring>
#include "ronin_log.h"

namespace Ronin::Kernel::HAL {

/**
 * Phase 1: Isolated Inference Data Flow
 * InferencePacket for streaming UTF-8 fragments (Optimized for Burmese).
 */
struct InferencePacket {
    uint32_t sequence_id;
    uint32_t token_id;
    char fragment[256]; // Increased to 256 bytes for long Burmese compound words
    float confidence;
    bool is_final;
};

/**
 * Lock-free SPSC Ring Buffer over Shared Memory.
 * Designed for low-latency communication between Kernel Core and Inference Module.
 */
struct SpineRingBuffer {
    static constexpr size_t kCapacity = 1024; // Power of two for fast masking
    std::atomic<size_t> head; // Producer (Inference Module) updates this
    std::atomic<size_t> tail; // Consumer (Kernel Core) updates this
    InferencePacket buffer[kCapacity];

    bool push(const InferencePacket& packet) {
        size_t h = head.load(std::memory_order_relaxed);
        size_t next_h = (h + 1) & (kCapacity - 1);
        if (next_h == tail.load(std::memory_order_acquire)) {
            return false; // Buffer full
        }
        buffer[h] = packet;
        head.store(next_h, std::memory_order_release);
        return true;
    }

    bool pop(InferencePacket& packet) {
        size_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) {
            return false; // Buffer empty
        }
        packet = buffer[t];
        tail.store((t + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }
};

template<typename T>
class SharedMemoryBridge {
public:
    SharedMemoryBridge(const std::string& name, size_t count = 1) 
        : m_name(name), m_size(sizeof(T) * count) {
    }

    ~SharedMemoryBridge() {
        if (m_ptr != MAP_FAILED) {
            munmap(m_ptr, m_size);
        }
        if (m_fd != -1) {
            close(m_fd);
        }
    }

    bool create(const std::string& base_path, bool is_producer = true) {
        // Dynamic path resolution for portability (Android vs CI/CD)
        std::string path;
        if (base_path.empty()) {
            // Fallback to current directory if no base path provided (e.g. in tests)
            path = m_name + ".shm";
        } else {
            path = base_path + "/" + m_name + ".shm";
        }
        
        m_fd = open(path.c_str(), O_RDWR | O_CREAT, 0666);
        if (m_fd == -1) {
            LOGE("SHM", "Failed to open SHM file: %s", path.c_str());
            return false;
        }

        if (is_producer) {
            if (ftruncate(m_fd, m_size) == -1) {
                LOGE("SHM", "Failed to ftruncate SHM file");
                return false;
            }
        }

        m_ptr = mmap(0, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        if (m_ptr == MAP_FAILED) {
            LOGE("SHM", "mmap failed");
            return false;
        }

        if (is_producer) {
            std::memset(m_ptr, 0, m_size);
        }

        return true;
    }

    T* get() { return static_cast<T*>(m_ptr); }

private:
    std::string m_name;
    size_t m_size;
    int m_fd = -1;
    void* m_ptr = MAP_FAILED;
};

} // namespace Ronin::Kernel::HAL
