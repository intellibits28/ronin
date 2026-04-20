#include "lora_engine.h"
#include "ronin_log.h"
#include <sys/mman.h>
#include <thread>
#include <algorithm>

#define TAG "RoninLora"

namespace Ronin::Kernel::Model {

LoraDispatcher::LoraDispatcher() : m_active_mask(0) {}

LoraDispatcher::~LoraDispatcher() {
    if (m_pinned_weights) {
        munlock(m_pinned_weights, m_pinned_size);
    }
}

bool LoraDispatcher::pinBaseWeights(void* weights, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (mlock(weights, size) != 0) {
        LOGE(TAG, "mlock failed for base model weights. Resident memory sovereignty compromised.");
        return false;
    }
    m_pinned_weights = weights;
    m_pinned_size = size;
    LOGI(TAG, "Base weights pinned successfully (%zu bytes).", size);
    return true;
}

bool LoraDispatcher::registerLora(const LoraDeltaBlock& block) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_registry[block.id] = block;
    LOGI(TAG, "Phase 4.1: Registered LoRA block ID %u (Interference: 0x%08X)", 
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
     * Interference Guard: O(1) Lookup.
     * Check if the new LoRA conflicts with currently active ones.
     */
    if (checkInterference(block)) {
        LOGE(TAG, "Interference Guard: Skill %u conflicts with active mask 0x%08X", 
             skill_id, m_active_mask);
        return false; 
    }

    /**
     * RULE 2: Zero-Stall Swap.
     * Zero-copy activation via bitmask.
     */
    m_active_mask |= (1 << (skill_id % 32));
    
    LOGI(TAG, "Zero-Stall Swap: Activated Skill %u (New Mask: 0x%08X)", 
         skill_id, m_active_mask);
    return true;
}

void LoraDispatcher::predictAndWarmup(const std::vector<uint32_t>& potential_ids) {
    // Launch background thread to warm-up potential LoRA adapters.
    std::thread([this, potential_ids]() {
        for (uint32_t id : potential_ids) {
            std::lock_guard<std::mutex> lock(this->m_mutex);
            auto it = m_registry.find(id);
            if (it != m_registry.end()) {
                const auto& b = it->second;
                if (b.A_matrix_delta) {
                    madvise(b.A_matrix_delta, b.rank * b.dim_in, MADV_WILLNEED);
                    madvise(b.B_matrix_delta, b.rank * b.dim_out, MADV_WILLNEED);
                }
            }
        }
        LOGI(TAG, "Predictive Paging: Warm-up complete for %zu potential adapters.", potential_ids.size());
    }).detach();
}

bool LoraDispatcher::checkInterference(const LoraDeltaBlock& new_block) const {
    return (m_active_mask & new_block.interference_signature) != 0;
}

} // namespace Ronin::Kernel::Model
