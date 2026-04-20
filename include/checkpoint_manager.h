#pragma once

#include <string>
#include <mutex>
#include <cstdint>
#include "checkpoint_schema_generated.h"

namespace Ronin::Kernel::Checkpoint {

/**
 * Phase 4.0: Graph Execution Checkpoint Model (LMK Survival).
 * Implements zero-copy serialization via FlatBuffers and kernel immunity via mmap/mlock.
 */
class CheckpointManager {
public:
    CheckpointManager(const std::string& path);
    ~CheckpointManager();

    // Kernel Immunity: Maps the checkpoint file into memory and pins critical segments.
    bool initialize();

    // Atomic Commit: Writes to .tmp, fsyncs, and renames to active checkpoint.
    // Includes GPS coordinates for Intent Anchoring (v4.1).
    bool commit(const std::string& intent_id, 
                uint64_t edge_frontier,
                const uint8_t* kv_data, 
                size_t kv_size,
                uint32_t lora_mask, 
                const std::string& plan_progress,
                double latitude,
                double longitude);

    /**
     * RULE 3: Re-awakening Protocol.
     * Uses Temporal Causal Stitching to decide between context continuation or new session.
     */
    bool stitchContext(const std::string& current_intent_id);

    // RESTORATION: Provides direct read-only access to the mmap'ed FlatBuffer.
    const Checkpoint* getActiveCheckpoint() const;

private:
    std::string m_path;
    int m_fd = -1;
    void* m_mapped_ptr = nullptr;
    size_t m_mapped_size = 0;
    mutable std::mutex m_mutex;

    void cleanup();
};

} // namespace Ronin::Kernel::Checkpoint
