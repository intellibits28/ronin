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
    CRITICAL = 3
};

/**
 * Phase 11.0 Hardening: Lexical Long-Term Memory (LTM)
 * Aligned with Single Gemma 4 Architecture. 
 * All E5/ONNX vector dependencies have been removed.
 */
class LongTermMemory {
public:
    explicit LongTermMemory(const std::string& db_path);
    ~LongTermMemory();

    // Prevent copying
    LongTermMemory(const LongTermMemory&) = delete;
    LongTermMemory& operator=(const LongTermMemory&) = delete;

    enum class RecallMode {
        FAST,     // Recent/Active memories only
        DEEP,     // Include Cold/Archived
        EXPLICIT  // Include everything including Forgotten
    };

    // Fact Storage (Key-Value)
    bool storeFact(const std::string& key, const std::string& value, MemoryPriority priority = MemoryPriority::MEDIUM);
    std::string retrieveFact(const std::string& key);

    // Message History
    bool storeMessage(const std::string& role, const std::string& content);
    std::vector<std::pair<std::string, std::string>> getHistory(int limit = 50, int offset = 0);

    // Cognitive Recall (FTS5 Keywords)
    std::vector<std::string> search(const std::string& query);
    
    // Memory Consolidation
    bool consolidate(const std::string& summary_text);

    // File Indexing (FTS5)
    bool indexFile(const std::string& name, const std::string& path, const std::string& ext, uint64_t modified);
    std::vector<std::string> searchFiles(const std::string& query);

    // Lifecycle Management
    void applyDecay(uint64_t current_timestamp);
    int runMaintenance(bool is_charging);

    // Auditing
    bool storeAuditLog(const std::string& action, const std::string& details);

private:
    sqlite3* m_db = nullptr;
    std::mutex m_mutex;
    double m_lambda = 0.000001; 

    bool initSchema();
};

} // namespace Ronin::Kernel::Memory
