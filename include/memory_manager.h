#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>

namespace Ronin::Kernel::Memory {

/**
 * Phase 11.0 Hardening: Lexical Memory Chunk
 * Removed embedding vectors. Relying on SQLite FTS5 and In-context window.
 */
struct MemoryChunk {
    uint32_t id;
    std::string text;
    uint64_t timestamp;
    float importance;
};

class MemoryManager {
public:
    explicit MemoryManager(size_t max_context_tokens = 2048);
    ~MemoryManager();

    // Prevent copying
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    /**
     * Phase 1.1: Context Hydration
     * Adds a new chunk of memory to the short-term sliding window.
     */
    void addMemory(const std::string& text, float importance = 1.0f);

    /**
     * Returns the full context string for the reasoning spine.
     */
    std::string getFullContext() const;

    /**
     * Clears all short-term context.
     */
    void clearContext();

    /**
     * Phase 9.2: Memory Pressure Handler
     * Trims old or low-importance chunks to fit within token limits.
     */
    void onMemoryPressure();

    /**
     * Calculates the current pressure score (0-100).
     */
    int getPressureScore() const;

    // LTM Bridge
    void setLongTermMemory(class LongTermMemory* ltm) { m_ltm = ltm; }

private:
    size_t m_max_tokens;
    std::deque<MemoryChunk> m_context_window;
    mutable std::mutex m_mutex;
    class LongTermMemory* m_ltm = nullptr;

    size_t estimateTokenCount(const std::string& text) const;
};

} // namespace Ronin::Kernel::Memory
