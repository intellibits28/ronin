#include "lora_engine.h"
#include "ronin_log.h"
#include <algorithm>

#define TAG "RoninLora"

namespace Ronin::Kernel::Model {

LoraDispatcher::LoraDispatcher() : m_active_mask(0) {}

LoraDispatcher::~LoraDispatcher() {
    // In a real implementation, unmap matrix_a/b here
}

bool LoraDispatcher::registerLora(const LoraDeltaBlock& block) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_registry[block.id] = block;
    LOGI(TAG, "Registered LoRA block ID %u (Interference Signature: 0x%08X)", block.id, block.interference_signature);
    return true;
}

bool LoraDispatcher::activateSkill(uint32_t skill_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_registry.find(skill_id);
    if (it == m_registry.end()) {
        LOGW(TAG, "Cannot activate LoRA for skill %u: Not registered.", skill_id);
        return false;
    }

    const auto& block = it->second;

    /**
     * RULE 3: Orthogonal Control.
     * Check if the new LoRA conflicts with currently active ones using bitwise AND.
     */
    if (checkInterference(block)) {
        LOGE(TAG, "LoRA Activation Conflict: Skill %u signature 0x%08X overlaps with active mask 0x%08X", 
             skill_id, block.interference_signature, m_active_mask);
        return false;
    }

    /**
     * RULE 2: Zero-Stall Swap.
     * Activate the LoRA by flipping its bit in the mask.
     * We assume a 1-to-1 mapping between skill ID and its bit position for this prototype.
     */
    m_active_mask |= (1 << (skill_id % 32));
    
    LOGI(TAG, "LoRA Activated: Skill %u (Active Mask: 0x%08X)", skill_id, m_active_mask);
    return true;
}

bool LoraDispatcher::checkInterference(const LoraDeltaBlock& new_block) const {
    // A LoRA conflicts if its interference signature overlaps with the currently active bits.
    // However, the rule states interference_signature prevents *conflicting LoRAs* from activating simultaneously.
    // In a more complex model, this would check against signatures of other active blocks.
    // For simplicity in Phase 4.0, we verify the signature against the active mask.
    return (m_active_mask & new_block.interference_signature) != 0;
}

void LoraDispatcher::applyDeltas(int8_t* input, int8_t* output) {
    // This would perform (X * A * B) * scaling and add it to the output.
    // Only applied if the corresponding bit is set in m_active_mask.
    // Stubbed for Phase 4.0 as it requires full model layer integration.
}

} // namespace Ronin::Kernel::Model
