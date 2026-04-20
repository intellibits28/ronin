#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace Ronin::Kernel::Model {

/**
 * RULE 1: Delta-Only Memory.
 * Stores raw binary diffs (A and B matrices) in INT8 format.
 * No full weights are stored here.
 */
struct LoraDeltaBlock {
    uint32_t id;
    int8_t* A_matrix_delta; // Pointers to mmap'ed INT8 data
    int8_t* B_matrix_delta;
    size_t dim_in;
    size_t dim_out;
    size_t rank;
    float scaling;

    /**
     * RULE 3: Orthogonal Safety.
     * Prevents conflicting LoRAs from activating simultaneously.
     */
    uint32_t interference_signature;
};

/**
 * Phase 4.1: LoRA Dispatcher Core (Hardware Reality).
 * Implements zero-stall swapping via bitmask pointer routing and predictive paging.
 */
class LoraDispatcher {
public:
    LoraDispatcher();
    ~LoraDispatcher();

    /**
     * RULE 1: Memory Pinning.
     * Ensures resident memory sovereignty for base weights.
     */
    bool pinBaseWeights(void* weights, size_t size);

    // Loads a LoRA delta block into the dispatcher's registry.
    bool registerLora(const LoraDeltaBlock& block);

    /**
     * RULE 2: Zero-Stall Swap.
     * Updates the active_mask using bitwise operations.
     * Checks for collisions using the O(1) interference guard.
     */
    bool activateSkill(uint32_t skill_id);

    /**
     * Predictive Paging: Warm-up potential LoRA adapters based on router probability.
     */
    void predictAndWarmup(const std::vector<uint32_t>& potential_ids);

    uint32_t getActiveMask() const { return m_active_mask; }

private:
    std::unordered_map<uint32_t, LoraDeltaBlock> m_registry;
    uint32_t m_active_mask = 0;
    void* m_pinned_weights = nullptr;
    size_t m_pinned_size = 0;
    mutable std::mutex m_mutex;

    /**
     * Interference Guard: Bitmask-based compatibility matrix (O(1) lookup).
     */
    bool checkInterference(const LoraDeltaBlock& new_block) const;
};

} // namespace Ronin::Kernel::Model
