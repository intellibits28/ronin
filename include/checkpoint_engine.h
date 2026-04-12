#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

namespace Ronin::Kernel::Checkpoint {

struct KVAnchor {
    uint32_t offset;
    uint32_t size;
};

struct TriAnchorModel {
    KVAnchor anchor1_prefix; // Pinned
    KVAnchor anchor2_working; // Quantized
    KVAnchor anchor3_recent; // Local context
};

class CheckpointEngine {
public:
    CheckpointEngine(const std::string& checkpoint_path);
    ~CheckpointEngine();

    // RAM-based shadow buffer via memfd_create
    bool initializeShadowBuffer(size_t size);

    // Update the shadow buffer (Staged Checkpoint logic)
    // Called every time a graph edge is traversed.
    bool updateCheckpointData(const uint8_t* data, size_t size);

    // Atomic flush to internal storage (/data/data/com.ronin.kernel/files/checkpoint.bin)
    bool persistToStorage();

    // LMK Signal Handler: Immediate flush
    void onLMKSignal();

private:
    std::string m_checkpoint_path;
    int m_memfd = -1;
    size_t m_buffer_size = 0;
    std::mutex m_mutex;
    
    // Internal helper to sync memfd to disk
    bool syncBufferToDisk();
};

} // namespace Ronin::Kernel::Checkpoint
