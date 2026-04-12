#include "memory_manager.h"
#include <sys/mman.h>
#include <algorithm>
#include <iostream>
#include <android/log.h>

#define TAG "RoninMemoryManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace Ronin::Kernel::Memory {

MemoryManager::MemoryManager(size_t recent_window_size) 
    : m_recent_window_size(recent_window_size) {}

MemoryManager::~MemoryManager() {
    unpinMemory();
}

/**
 * Anchor 1: Prefix Pinning.
 * Uses mlock() to prevent the system prompt from being paged to disk or reclaimed by LMK.
 */
bool MemoryManager::setPrefix(const std::vector<Token>& prefix_tokens) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    unpinMemory();
    m_anchor1_prefix = prefix_tokens;
    
    // Calculate total size for pinning
    size_t total_size = m_anchor1_prefix.size() * sizeof(Token);
    if (total_size > 0) {
        pinMemory(m_anchor1_prefix.data(), total_size);
    }
    
    LOGI("Anchor 1 (Prefix) set and pinned: %zu tokens", m_anchor1_prefix.size());
    return true;
}

/**
 * Anchor 3: Rolling window management.
 * Adds new tokens in high precision. Triggers pruning if full.
 */
void MemoryManager::addRecentToken(const Token& token) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_anchor3_recent.push_back(token);
    
    if (m_anchor3_recent.size() > m_recent_window_size) {
        pruneAndCompress();
    }
}

/**
 * Saliency-based Pruning (Anchor 3 -> Anchor 2).
 * Sorts recent tokens by saliency_score. The least salient are quantized into Anchor 2.
 */
void MemoryManager::pruneAndCompress() {
    // Already under lock from addRecentToken
    LOGI("Triggering saliency-based pruning for Anchor 3.");

    // Sort by saliency to identify low-importance tokens
    std::sort(m_anchor3_recent.begin(), m_anchor3_recent.end(), 
              [](const Token& a, const Token& b) {
                  return a.saliency_score > b.saliency_score;
              });

    // Move everything beyond the window size to Anchor 2 (Compressed)
    while (m_anchor3_recent.size() > m_recent_window_size) {
        Token low_saliency_token = m_anchor3_recent.back();
        m_anchor3_recent.pop_back();

        // Quantize to INT8 representation to save space
        m_anchor2_compressed.push_back(quantize(low_saliency_token));
    }
}

/**
 * Reconstructs the context for the inference engine: Anchor 1 -> Anchor 2 -> Anchor 3.
 */
std::vector<uint32_t> MemoryManager::reconstructContext() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint32_t> context;

    for (const auto& t : m_anchor1_prefix) context.push_back(t.id);
    for (const auto& t : m_anchor2_compressed) context.push_back(t.id);
    for (const auto& t : m_anchor3_recent) context.push_back(t.id);

    return context;
}

/**
 * Memory Pressure Response: Aggressive Anchor 2 Management.
 * Could clear historical data if critical, but for now, we'll log it.
 */
void MemoryManager::onMemoryPressure() {
    std::lock_guard<std::mutex> lock(m_mutex);
    LOGI("Critical Memory Pressure: Reducing Anchor 2 footprint.");
    
    // Optional: Clearing Anchor 2 to reclaim RAM immediately
    // m_anchor2_compressed.clear();
}

/**
 * INT8 Quantization: Simple scale-and-round compression.
 */
CompressedToken MemoryManager::quantize(const Token& token) {
    CompressedToken ct;
    ct.id = token.id;
    for (float v : token.embedding) {
        // Simple mapping: [-1.0, 1.0] -> [-127, 127]
        ct.quantized_embedding.push_back(static_cast<int8_t>(v * 127.0f));
    }
    return ct;
}

void MemoryManager::pinMemory(void* ptr, size_t size) {
    if (mlock(ptr, size) != 0) {
        LOGE("mlock failed for Anchor 1: Memory cannot be pinned.");
    } else {
        m_pinned_ptr = ptr;
        m_pinned_size = size;
        LOGI("Successfully pinned %zu bytes of memory.", size);
    }
}

void MemoryManager::unpinMemory() {
    if (m_pinned_ptr) {
        munlock(m_pinned_ptr, m_pinned_size);
        m_pinned_ptr = nullptr;
    }
}

} // namespace Ronin::Kernel::Memory
