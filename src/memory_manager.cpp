#include "memory_manager.h"
#include <ctime>
#include <numeric>
#include <algorithm>
#include "ronin_log.h"

#define TAG "RoninMemoryManager"

namespace Ronin::Kernel::Memory {

MemoryManager::MemoryManager(size_t max_context_tokens) 
    : m_max_tokens(max_context_tokens) {}

MemoryManager::~MemoryManager() = default;

void MemoryManager::addMemory(const std::string& text, float importance) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    MemoryChunk chunk;
    chunk.id = static_cast<uint32_t>(m_context_window.size());
    chunk.text = text;
    chunk.timestamp = std::time(nullptr);
    chunk.importance = importance;

    m_context_window.push_back(chunk);

    // Maintain context window size
    onMemoryPressure();
}

std::string MemoryManager::getFullContext() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string context;
    for (const auto& chunk : m_context_window) {
        context += chunk.text + "\n";
    }
    return context;
}

void MemoryManager::clearContext() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_context_window.clear();
}

size_t MemoryManager::estimateTokenCount(const std::string& text) const {
    // Basic heuristic: 1 token ~= 4 chars for English, but for Myanmar 
    // it's different. We use a safe upper bound here.
    return text.length() / 2; 
}

void MemoryManager::onMemoryPressure() {
    size_t total_tokens = 0;
    for (const auto& chunk : m_context_window) {
        total_tokens += estimateTokenCount(chunk.text);
    }

    while (total_tokens > m_max_tokens && !m_context_window.empty()) {
        total_tokens -= estimateTokenCount(m_context_window.front().text);
        m_context_window.pop_front();
    }
}

int MemoryManager::getPressureScore() const {
    size_t total_tokens = 0;
    for (const auto& chunk : m_context_window) {
        total_tokens += estimateTokenCount(chunk.text);
    }
    return static_cast<int>((total_tokens * 100) / m_max_tokens);
}

} // namespace Ronin::Kernel::Memory
