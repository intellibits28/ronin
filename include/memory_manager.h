#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <mutex>
#include <array>
#include "long_term_memory.h"

namespace Ronin::Kernel::Memory {

template<typename T, size_t Size>
class CircularBuffer {
public:
    void push(const T& item) {
        m_data[m_head] = item;
        m_head = (m_head + 1) % Size;
        if (m_count < Size) m_count++;
    }

    const T& operator[](size_t index) const {
        return m_data[(m_head - m_count + index + Size) % Size];
    }

    size_t size() const { return m_count; }
    void clear() { m_head = 0; m_count = 0; }

private:
    std::array<T, Size> m_data;
    size_t m_head = 0;
    size_t m_count = 0;
};

enum class AnchorType {
    PREFIX,     // Anchor 1: Pinned
    COMPRESSED, // Anchor 2: Historical/Quantized
    RECENT      // Anchor 3: High-precision window
};

struct Token {
    uint32_t id;
    float saliency_score;
    std::vector<float> embedding; // High-precision representation
};

struct CompressedToken {
    uint32_t id;
    std::vector<int8_t> quantized_embedding; // INT8 representation
};

class MemoryManager {
public:
    MemoryManager(size_t recent_window_size);
    ~MemoryManager();

    // Anchor 1: System/Identity initialization (mlock pinned)
    bool setPrefix(const std::vector<Token>& prefix_tokens);

    // Anchor 3: Add new tokens to the recent window
    void addRecentToken(const Token& token);

    // Saliency-based Pruning: Move low-saliency tokens to Anchor 2
    void pruneAndCompress();

    // Reconstruct the full prompt context for the inference engine
    std::vector<uint32_t> reconstructContext();

    // Returns current memory pressure score (0-100)
    int getPressureScore() const;

    // Link with L3 Deep-store for consolidation
    void setLongTermMemory(LongTermMemory* ltm) { m_l3_store = ltm; }

    // LMK Signal: Force aggressive compression of Anchor 2
    void onMemoryPressure();

    // Context Cleanup: Clear all anchors
    void clearContext();

    // Deduplication filter for search results
    static std::vector<std::string> filterDuplicateFilenames(const std::vector<std::string>& results);

private:
    size_t m_recent_window_size;
    std::mutex m_mutex;
    LongTermMemory* m_l3_store = nullptr;

    // Anchor 1: Pinned Prefix (RAM)
    std::vector<Token> m_anchor1_prefix;
    void* m_pinned_ptr = nullptr;
    size_t m_pinned_size = 0;

    // Anchor 2: Compressed History (INT8)
    std::vector<CompressedToken> m_anchor2_compressed;

    // Anchor 3: High-precision Rolling Window
    std::vector<Token> m_anchor3_recent;

    // NullClaw Minimalist Requirement: Fixed-size Circular Buffer for recent context
    CircularBuffer<uint32_t, 128> m_recent_context_buffer;

    // Internal helpers
    CompressedToken quantize(const Token& token);
    void pinMemory(void* ptr, size_t size);
    void unpinMemory();
};

} // namespace Ronin::Kernel::Memory
