#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <sqlite3.h>

namespace Ronin::Kernel::Memory {

enum class MemoryPriority : int {
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2,
    CRITICAL = 3 // Never forgotten
};

struct Fact {
    std::string key;
    std::string value;
    double stability;
    uint64_t last_accessed_timestamp;
    MemoryPriority priority;
};

class LongTermMemory {
public:
    LongTermMemory(const std::string& db_path);
    ~LongTermMemory();

    // Store a fact with an explicit priority level
    bool storeFact(const std::string& key, const std::string& value, MemoryPriority priority = MemoryPriority::MEDIUM);

    // Retrieve a fact with Temporal Decay applied
    std::string retrieveFact(const std::string& key);

    // Natural Forgetting: Background maintenance to prune stale, low-priority memories
    // Returns the number of pruned items.
    int runMaintenance(bool is_charging);

    // Apply Temporal Decay: S(t) = e^(-lambda * t)
    void applyDecay(uint64_t current_timestamp);

    std::vector<std::string> search(const std::string& query);
    bool consolidate(const std::string& summary_text);

    // File Indexing (FTS5 + Semantic)
    struct FileEmbedding {
        std::string name;
        std::vector<float> vector;
    };

    bool indexFile(const std::string& name, const std::string& path, const std::string& ext, uint64_t modified, const std::vector<float>& embedding = {});
    std::vector<std::string> searchFiles(const std::string& query);
    std::vector<FileEmbedding> getAllFileEmbeddings();

private:
    sqlite3* m_db = nullptr;
    std::mutex m_mutex;
    double m_lambda = 0.000001; // Slower decay for long-term storage

    bool initSchema();
};

} // namespace Ronin::Kernel::Memory
