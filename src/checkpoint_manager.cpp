#include "checkpoint_manager.h"
#include "capabilities/hardware_bridge.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include "ronin_log.h"

#define TAG "RoninCheckpoint"

namespace Ronin::Kernel::Checkpoint {

CheckpointManager::CheckpointManager(const std::string& path) 
    : m_path(path) {}

CheckpointManager::~CheckpointManager() {
    cleanup();
}

void CheckpointManager::cleanup() {
    if (m_mapped_ptr && m_mapped_ptr != MAP_FAILED) {
        // Unlock memory before unmapping
        size_t lock_size = std::min(m_mapped_size, (size_t)4096);
        munlock(m_mapped_ptr, lock_size);
        munmap(m_mapped_ptr, m_mapped_size);
        m_mapped_ptr = nullptr;
    }
    if (m_fd != -1) {
        close(m_fd);
        m_fd = -1;
    }
}

bool CheckpointManager::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    cleanup();

    m_fd = open(m_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (m_fd == -1) {
        LOGW(TAG, "No existing checkpoint found for mmap at %s", m_path.c_str());
        return false;
    }

    struct stat st;
    if (fstat(m_fd, &st) == -1 || st.st_size == 0) {
        close(m_fd);
        m_fd = -1;
        return false;
    }

    m_mapped_size = st.st_size;
    m_mapped_ptr = mmap(nullptr, m_mapped_size, PROT_READ, MAP_PRIVATE, m_fd, 0);
    if (m_mapped_ptr == MAP_FAILED) {
        std::string errorMsg = "> SURVIVAL ERROR: mmap failed for checkpoint.";
        LOGE(TAG, "%s", errorMsg.c_str());
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(errorMsg);
        close(m_fd);
        m_fd = -1;
        return false;
    }

    /**
     * RULE 2: Kernel Immunity.
     * Use mlock() on critical segments (header and frontier bitmask) 
     * to prevent the OS from paging them out during high memory pressure.
     */
    size_t lock_size = std::min(m_mapped_size, (size_t)4096);
    if (mlock(m_mapped_ptr, lock_size) != 0) {
        std::string logMsg = "> SURVIVAL WARNING: mlock failed. Header not immune.";
        LOGW(TAG, "%s", logMsg.c_str());
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
    } else {
        std::string logMsg = "> SURVIVAL CORE: Kernel Immunity active (mlock success).";
        LOGI(TAG, "%s", logMsg.c_str());
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
    }

    Ronin::Kernel::Capability::HardwareBridge::pushMessage("> SURVIVAL CORE: Hydration ready from " + m_path);
    return true;
}

bool CheckpointManager::commit(const std::string& intent_id, 
                               uint64_t edge_frontier,
                               const uint8_t* kv_data, 
                               size_t kv_size,
                               uint32_t lora_mask, 
                               const std::string& plan_progress) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // RULE 1: Zero-Copy Serialization via FlatBuffers
    flatbuffers::FlatBufferBuilder builder(kv_size + 1024);
    
    auto intent_off = builder.CreateString(intent_id);
    auto plan_off = builder.CreateString(plan_progress);
    auto kv_off = builder.CreateVector(kv_data, kv_size);

    CheckpointBuilder cb(builder);
    cb.add_version(1);
    cb.add_magic(0x524F4E4E); // "RONN"
    cb.add_intent_id(intent_off);
    cb.add_edge_frontier(edge_frontier);
    cb.add_kv_state(kv_off);
    cb.add_lora_mask(lora_mask);
    cb.add_plan_progress(plan_off);

    auto root = cb.Finish();
    builder.Finish(root);

    uint8_t* buf = builder.GetBufferPointer();
    size_t size = builder.GetSize();

    /**
     * RULE 3: Atomic Writes.
     * Write to .tmp, fsync, and rename to overwrite active checkpoint.
     */
    std::string tmp_path = m_path + ".tmp";
    int tfd = open(tmp_path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (tfd == -1) {
        LOGE(TAG, "Failed to create atomic tmp checkpoint.");
        return false;
    }

    if (write(tfd, buf, size) != static_cast<ssize_t>(size)) {
        LOGE(TAG, "Incomplete write to tmp checkpoint.");
        close(tfd);
        unlink(tmp_path.c_str());
        return false;
    }

    // Force persistence to storage media
    fsync(tfd);
    close(tfd);

    // Atomic rename ensures consistent state on disk
    if (rename(tmp_path.c_str(), m_path.c_str()) == -1) {
        std::string errorMsg = "> SURVIVAL ERROR: Atomic rename failed.";
        LOGE(TAG, "%s", errorMsg.c_str());
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(errorMsg);
        return false;
    }

    std::string logMsg = "> SURVIVAL CORE: Atomic commit complete (" + std::to_string(size) + " bytes).";
    LOGI(TAG, "%s", logMsg.c_str());
    Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
    
    return true; 
}

const Checkpoint* CheckpointManager::getActiveCheckpoint() const {
    if (!m_mapped_ptr || m_mapped_ptr == MAP_FAILED) return nullptr;
    return GetCheckpoint(m_mapped_ptr);
}

} // namespace Ronin::Kernel::Checkpoint
