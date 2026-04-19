#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace Ronin::Kernel::Model {

/**
 * RULE 1: Delta-Only Storage.
 * Stores raw binary diffs (A and B matrices) in INT8 format.
 */
struct LoraDeltaBlock {
    uint32_t id;
    int8_t* matrix_a; // Pointers to mmap'ed INT8 data
    int8_t* matrix_b;
    size_t dim_in;
    size_t dim_out;
    size_t rank;
    float scaling;

    /**
     * RULE 3: Orthogonal Control.
     * Prevents conflicting LoRAs from activating simultaneously.
     */
    uint32_t interference_signature;
};

/**
 * Phase 4.0: LoRA State Diff Serialization & Activation Masking.
 * Implements zero-stall swapping via pointer masks.
 */
class LoraDispatcher {
public:
    LoraDispatcher();
    ~LoraDispatcher();

    // Loads a LoRA delta block into the dispatcher's registry.
    // In a full implementation, this would handle mmap'ing the raw binary.
    bool registerLora(const LoraDeltaBlock& block);

    /**
     * RULE 2: Zero-Stall Swap.
     * Updates the active_mask using bitwise operations.
     */
    bool activateSkill(uint32_t skill_id);

    uint32_t getActiveMask() const { return m_active_mask; }

    // Logic for forward pass integration (Stub for Phase 4.0)
    void applyDeltas(int8_t* input, int8_t* output);

private:
    std::unordered_map<uint32_t, LoraDeltaBlock> m_registry;
    uint32_t m_active_mask = 0;
    mutable std::mutex m_mutex;

    // Checks if the new LoRA conflicts with currently active ones.
    bool checkInterference(const LoraDeltaBlock& new_block) const;
};

} // namespace Ronin::Kernel::Model
