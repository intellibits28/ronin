#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <sqlite3.h>
#include "myanmar_segmenter.h"

namespace Ronin::Kernel::Memory {

enum class MemoryPriority : int {
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2,
    CRITICAL = 3
};

/**
 * v10.0 Memory Tiers for classified storage.
 */
enum class MemoryTier : int {
    NOTE = 0,
    FACT = 1,
    VAULT = 2
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

    // Classified Storage (v10.0)
    bool storeNote(const std::string& content);
    bool storeFact(const std::string& entity, const std::string& attr, const std::string& value);
    bool storeVault(const std::string& key, const std::string& encrypted_value);
    std::string retrieveFact(const std::string& entity, const std::string& attr);

    // Message History
    bool storeMessage(const std::string& role, const std::string& content, int64_t timestamp = 0);
    std::vector<std::pair<std::string, std::string>> getHistory(int limit = 50, int offset = 0);
    bool clearHistory();

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

    // Segmenter Control
    bool loadSegmenter(const std::string& dict_path);
    Ronin::Kernel::NLP::MyanmarSegmenter* getSegmenter() { return m_segmenter.get(); }

private:
    sqlite3* m_db = nullptr;
    std::mutex m_mutex;
    double m_lambda = 0.000001; 
    std::unique_ptr<Ronin::Kernel::NLP::MyanmarSegmenter> m_segmenter;

    bool initSchema();
};

} // namespace Ronin::Kernel::Memory
