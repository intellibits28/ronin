#include "lora_engine.h"
#include "ronin_log.h"

#define TAG "RoninLora"

namespace Ronin::Kernel::Model {

LoraDispatcher::LoraDispatcher() : m_active_mask(0) {}

LoraDispatcher::~LoraDispatcher() {
    // Matrix pointers are mmap'ed; cleanup handled by owner or specific unmap logic.
}

bool LoraDispatcher::registerLora(const LoraDeltaBlock& block) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_registry[block.id] = block;
    LOGI(TAG, "Phase 2: Registered LoRA block ID %u (Interference: 0x%08X)", 
         block.id, block.interference_signature);
    return true;
}

bool LoraDispatcher::activateSkill(uint32_t skill_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_registry.find(skill_id);
    if (it == m_registry.end()) {
        LOGW(TAG, "Cannot activate LoRA: Skill %u not found in registry.", skill_id);
        return false;
    }

    const auto& block = it->second;

    /**
     * RULE 3: Orthogonal Safety.
     * Check if the new LoRA conflicts with currently active ones.
     */
    if (checkInterference(block)) {
        LOGE(TAG, "RULE 3 VIOLATION: Skill %u conflicts with active LoRAs (Mask: 0x%08X)", 
             skill_id, m_active_mask);
        return false; // Explicitly reject collision
    }

    /**
     * RULE 2: Zero-Stall Swap.
     * Flip the bit in the mask corresponding to this skill ID.
     */
    m_active_mask |= (1 << (skill_id % 32));
    
    LOGI(TAG, "Zero-Stall Swap: Activated Skill %u (New Mask: 0x%08X)", 
         skill_id, m_active_mask);
    return true;
}

bool LoraDispatcher::checkInterference(const LoraDeltaBlock& new_block) const {
    // Collision detected if new block's signature overlaps with existing mask.
    return (m_active_mask & new_block.interference_signature) != 0;
}

} // namespace Ronin::Kernel::Model
