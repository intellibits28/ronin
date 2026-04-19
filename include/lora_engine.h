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
 * Phase 2: LoRA State Diff Serialization & Activation Masking.
 * Implements zero-stall swapping via bitmask pointer routing.
 */
class LoraDispatcher {
public:
    LoraDispatcher();
    ~LoraDispatcher();

    // Loads a LoRA delta block into the dispatcher's registry.
    bool registerLora(const LoraDeltaBlock& block);

    /**
     * RULE 2: Zero-Stall Swap.
     * Updates the active_mask using bitwise operations.
     * Checks for collisions using the interference_signature.
     */
    bool activateSkill(uint32_t skill_id);

    uint32_t getActiveMask() const { return m_active_mask; }

private:
    std::unordered_map<uint32_t, LoraDeltaBlock> m_registry;
    uint32_t m_active_mask = 0;
    mutable std::mutex m_mutex;

    // Checks if the new LoRA conflicts with currently active ones.
    bool checkInterference(const LoraDeltaBlock& new_block) const;
};

} // namespace Ronin::Kernel::Model
