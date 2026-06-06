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
    VAULT = 2,
    EPISODE = 3,
    PREDICTION = 4
};

/**
 * v11.3 Fact Source Tracking.
 */
enum class SourceType : int {
    USER_EXPLICIT = 0,
    USER_INFERRED = 1,
    OCR = 2,
    IMPORTED = 3
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

    // Agent-first Notes Architecture (v10.1 / v13.0 refactored)
    bool storeNote(const std::string& title, const std::string& content, const std::string& tags = "");
    bool storeFact(const std::string& entity, const std::string& attr, const std::string& value, 
                  SourceType source = SourceType::USER_EXPLICIT, float confidence = 1.0f);
    bool storeVault(const std::string& title, const std::string& encrypted_blob);
    std::string lookupVault(const std::string& title);
    
    // v13.0: Enhanced Episodic and Prediction Storage
    bool storeEpisode(const std::string& intent, const std::string& summary, const std::string& payload_json, 
                      bool success, const std::string& goal_id = "", const std::string& node_id = "",
                      int64_t latency_ms = 0, float conf_before = 0.0f, float conf_after = 0.0f);
                      
    bool storePrediction(const std::string& goal_id, const std::string& node_id, 
                         const std::string& predicted_json, const std::string& actual_json, float error_score);
    
    std::string lookupFact(const std::string& entity, const std::string& attr);
    std::vector<std::string> searchNotes(const std::string& query);
    std::vector<std::string> searchEpisodes(const std::string& query);
    std::vector<std::string> getNotesList();
    std::vector<std::pair<std::string, std::string>> getFactsList();

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
