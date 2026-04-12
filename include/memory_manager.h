#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <mutex>

namespace Ronin::Kernel::Memory {

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

    // LMK Signal: Force aggressive compression of Anchor 2
    void onMemoryPressure();

private:
    size_t m_recent_window_size;
    std::mutex m_mutex;

    // Anchor 1: Pinned Prefix (RAM)
    std::vector<Token> m_anchor1_prefix;
    void* m_pinned_ptr = nullptr;
    size_t m_pinned_size = 0;

    // Anchor 2: Compressed History (INT8)
    std::vector<CompressedToken> m_anchor2_compressed;

    // Anchor 3: High-precision Rolling Window
    std::vector<Token> m_anchor3_recent;

    // Internal helpers
    CompressedToken quantize(const Token& token);
    void pinMemory(void* ptr, size_t size);
    void unpinMemory();
};

} // namespace Ronin::Kernel::Memory
