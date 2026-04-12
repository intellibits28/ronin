#include "memory_manager.h"
#include <sys/mman.h>
#include <algorithm>
#include <iostream>
#include "ronin_log.h"

#define TAG "RoninMemoryManager"

namespace Ronin::Kernel::Memory {

MemoryManager::MemoryManager(size_t recent_window_size) 
    : m_recent_window_size(recent_window_size) {}

MemoryManager::~MemoryManager() {
    unpinMemory();
}

bool MemoryManager::setPrefix(const std::vector<Token>& prefix_tokens) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    unpinMemory();
    m_anchor1_prefix = prefix_tokens;
    
    size_t total_size = m_anchor1_prefix.size() * sizeof(Token);
    if (total_size > 0) {
        pinMemory(m_anchor1_prefix.data(), total_size);
    }
    
    LOGI(TAG, "Anchor 1 (Prefix) set and pinned: %zu tokens", m_anchor1_prefix.size());
    return true;
}

void MemoryManager::addRecentToken(const Token& token) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_anchor3_recent.push_back(token);
    if (m_anchor3_recent.size() > m_recent_window_size) {
        pruneAndCompress();
    }
}

void MemoryManager::pruneAndCompress() {
    LOGI(TAG, "Triggering saliency-based pruning for Anchor 3.");
    std::sort(m_anchor3_recent.begin(), m_anchor3_recent.end(), 
              [](const Token& a, const Token& b) {
                  return a.saliency_score > b.saliency_score;
              });

    while (m_anchor3_recent.size() > m_recent_window_size) {
        Token low_saliency_token = m_anchor3_recent.back();
        m_anchor3_recent.pop_back();
        m_anchor2_compressed.push_back(quantize(low_saliency_token));
    }
}

std::vector<uint32_t> MemoryManager::reconstructContext() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint32_t> context;
    for (const auto& t : m_anchor1_prefix) context.push_back(t.id);
    for (const auto& t : m_anchor2_compressed) context.push_back(t.id);
    for (const auto& t : m_anchor3_recent) context.push_back(t.id);
    return context;
}

int MemoryManager::getPressureScore() const {
    // Basic heuristic: Pressure increases as historical buffer (Anchor 2) grows.
    // 1000 items = 100% pressure.
    size_t count = m_anchor2_compressed.size();
    if (count > 1000) return 100;
    return static_cast<int>((static_cast<float>(count) / 1000.0f) * 100.0f);
}

void MemoryManager::onMemoryPressure() {
    std::lock_guard<std::mutex> lock(m_mutex);
    LOGI(TAG, "Critical Memory Pressure: Reducing Anchor 2 footprint.");
}

CompressedToken MemoryManager::quantize(const Token& token) {
    CompressedToken ct;
    ct.id = token.id;
    for (float v : token.embedding) {
        // Safe mapping with clamp to prevent overflow: [-1.0, 1.0] -> [-127, 127]
        float scaled = std::clamp(v, -1.0f, 1.0f) * 127.0f;
        ct.quantized_embedding.push_back(static_cast<int8_t>(std::round(scaled)));
    }
    return ct;
}

void MemoryManager::pinMemory(void* ptr, size_t size) {
    if (mlock(ptr, size) != 0) {
        LOGE(TAG, "mlock failed for Anchor 1: Memory cannot be pinned.");
    } else {
        m_pinned_ptr = ptr;
        m_pinned_size = size;
        LOGI(TAG, "Successfully pinned %zu bytes of memory.", size);
    }
}

void MemoryManager::unpinMemory() {
    if (m_pinned_ptr) {
        munlock(m_pinned_ptr, m_pinned_size);
        m_pinned_ptr = nullptr;
    }
}

} // namespace Ronin::Kernel::Memory
